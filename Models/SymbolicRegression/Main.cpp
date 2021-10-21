
// ./main --time=1h --inner-time=5s --explore=0.1 --thin=0 --threads=1 --input=./data-sources/Science/Zipf/data.txt --tree=./out/tree.txt

#include <cmath>
#include "Random.h"

using D = double;

const double sdscale  = 1.0; // can change if we want
size_t nsamples = 100; // how many per structure?
size_t nstructs = 100; // print out all the samples from the top this many structures
int    polynomial_degree = -1; //-1 means do everything, otherwise store ONLY polynomials less than or equal to this bound

const size_t trim_at = 5000; // when we get this big in overall_sample structures
const size_t trim_to = 1000;  // trim to this size

size_t inner_restarts = 5000;

// We define these here so we can substitute normal and cauchy or something else if we want
double constant_propose(double c, double s) { return c+s*random_normal(); } // should be symmetric for use below
double constant_prior(double c)   { return normal_lpdf(c);}
//double constant_propose(double c) { return c+random_cauchy(); } // should be symmetric for use below
//double constant_prior(double c)   { return cauchy_lpdf(c);}

// Proposals and random generation happen on a variety of scales
double random_scale() {
	double scale = 1.0;
	switch(myrandom(7)){
		case 0: scale = 100.0;  break;
		case 1: scale =  10.0;   break;
		case 2: scale =   1.0;   break;
		case 3: scale =   0.10;  break;
		case 4: scale =   0.01;  break;
		case 5: scale =   0.001; break;
		case 6: scale =   0.0001; break;
		default: assert(false);
	}
	return scale; 
}


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "Singleton.h"
#include "Grammar.h"

// We need these to declare this so it is defined before grammar,
// and so these can be accessed in grammar.
// What a goddamn nightmare (the other option is to separate header files)
struct ConstantContainer {
	std::vector<D> constants;
	size_t         constant_idx; // in evaluation, this variable stores what constant we are in 
};


class MyGrammar : public Grammar<D,D, D>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		add("(%s+%s)",    +[](D a, D b) -> D     { return a+b; }),
		add("(%s-%s)",    +[](D a, D b) -> D     { return a-b; }),
		add("(%s*%s)",    +[](D a, D b) -> D     { return a*b; }),
		add("(%s/%s)",    +[](D a, D b) -> D     { return (b==0 ? 0 : a/b); }),
		
		add("pow(%s,%s)",    +[](D a, D b) -> D     { return pow(a,b); }),
		
		add("(-%s)",      +[](D a)          -> D { return -a; }),
		add("exp(%s)",    +[](D a)          -> D { return exp(a); }),
		add("log(%s)",    +[](D a)          -> D { return log(a); }),
		
		add("1",          +[]()             -> D { return 1.0; }),
		
		// give the type to add and then a vms function
		add_vms<D>("C", new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
						
				// Here we are going to use a little hack -- we actually know that vms->program_loader
				// is of type MyHypothesis, so we will cast to that
				auto* h = dynamic_cast<ConstantContainer*>(vms->program.loader);
				if(h == nullptr) { assert(false); }
				else {
					assert(h->constant_idx < h->constants.size()); 
					vms->template push<D>(h->constants.at(h->constant_idx++));
				}
		}), 5.0);
							
		add("x",             Builtins::X<MyGrammar>, 5.0);
	}					  
					  
} grammar;

// check if a rule is constant
bool isConstant(const Rule* r) { return r->format == "C"; }

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public ConstantContainer,
						   public LOTHypothesis<MyHypothesis,D,D,MyGrammar,&grammar> {
	/* This class handles enumeration of the structure and critically does MCMC over the constants */
	
public:

	using Super = LOTHypothesis<MyHypothesis,D,D,MyGrammar,&grammar>;
	using Super::Super;

	virtual D callOne(const D x, const D err) {
		// We need to override this because LOTHypothesis::callOne asserts that the program is non-empty
		// but actually ours can be if we are only a constant. 
		// my own wrapper that zeros the constant_i counter
		
		constant_idx = 0;
		const auto out = Super::callOne(x,err);
		assert(constant_idx == constants.size()); // just check we used all constants
		return out;
	}
	

	double compute_single_likelihood(const datum_t& datum) override {
		
		double fx = this->callOne(datum.input, NaN);
		
		if(std::isnan(fx)) 
			return -infinity;
            
		return normal_lpdf( (fx-datum.output)/datum.reliability );		
	}
	
	size_t count_constants() const {
		size_t cnt = 0;
		for(const auto& x : value) {
			cnt += isConstant(x.rule);
		}
		return cnt;
	}
	
	virtual double compute_constants_prior() const {
		// NOTE: because of fb below, this must be computed the same way as sampling (which is normal)
		double lp = 0.0;
		for(auto& c : constants) {
			lp += constant_prior(c);
		}
		return lp;
	}
	
	virtual double compute_prior() override {
		this->prior = Super::compute_prior() + compute_constants_prior();
		return this->prior;
	}
	
	virtual std::string __my_string_recurse(const Node* n, size_t& idx) const {
		// we need this to print strings -- its in a similar format to evaluation
		if(isConstant(n->rule)) {
			return "("+to_string_with_precision(constants[idx++], 14)+")";
		}
		else if(n->rule->N == 0) {
			return n->rule->format;
		}
		else {
			
			// strings are evaluated in right->left order so we have to 
			// use that here (since we use them to index idx)
			std::vector<std::string> childStrings(n->nchildren());
			
			/// recurse on the children. NOTE: they are linearized left->right, 
			// which means that they are popped 
			for(size_t i=0;i<n->rule->N;i++) {
				childStrings[i] = __my_string_recurse(&n->child(i),idx);
			}
			
			std::string s = n->rule->format;
			for(size_t i=0;i<n->rule->N;i++) { // can't be size_t for counting down
				auto pos = s.find(Rule::ChildStr);
				assert(pos != std::string::npos); // must contain the ChildStr for all children all children
				s.replace(pos, Rule::ChildStr.length(), childStrings[i]);
			}
			
			return s;
		}
	}
	
	virtual std::string string(std::string prefix="") const override { 
		// we can get here where our constants have not been defined it seems...
		if(not this->is_evaluable()) 
			return structure_string(); // don't fill in constants if we aren't complete
		
		assert(constants.size() == count_constants()); // or something is broken
		
		size_t idx = 0;
		return  prefix+std::string("\u03BBx.") +  __my_string_recurse(&value, idx);
	}
	
	virtual std::string structure_string(bool usedot=true) const {
		return Super::string("", usedot);
	}
	
	/// *****************************************************************************
	/// Change equality to include equality of constants
	/// *****************************************************************************
	
	virtual bool operator==(const MyHypothesis& h) const override {
		// equality requires our constants to be equal 
		return this->Super::operator==(h) and
			   constants == h.constants;
	}

	virtual size_t hash() const override {
		// hash includes constants so they are only ever equal if constants are equal
		size_t h = Super::hash();
		for(size_t i=0;i<constants.size();i++) {
			hash_combine(h, i, constants[i]);
		}
		return h;
	}
	
	/// *****************************************************************************
	/// Implement MCMC moves as changes to constants
	/// *****************************************************************************
	
	virtual std::pair<MyHypothesis,double> propose() const override {
		// Our proposals will either be to constants, or entirely from the prior
		// Note that if we have no constants, we will always do prior proposals
		auto NC = count_constants();
		
		if(NC != 0 and flip(0.95)){
			MyHypothesis ret = *this;
			
			// now add to all that I have
			for(size_t i=0;i<NC;i++) { 
				ret.constants[i] = constant_propose(ret.constants[i], random_scale()); // symmetric, not counting in fb
			}
			return std::make_pair(ret, 0.0);
		}
		else {
			auto [ret, fb] = Super::propose(); // a proposal to structure
			
			ret.randomize_constants(); // with random constants -- this resizes so that it's right for propose
			
			return std::make_pair(ret, fb + ret.compute_constants_prior() - this->compute_constants_prior());
		}
	}
	
	virtual void randomize_constants() {
		// NOTE: Because of how fb is computed in propose, we need to make this the same as the prior
		constants.resize(count_constants());
		for(size_t i=0;i<constants.size();i++) {			
			constants[i] = constant_propose(0, random_scale());
		}
	}

	virtual MyHypothesis restart() const override {
		MyHypothesis ret = Super::restart(); // may reset my structure
		ret.randomize_constants();
		return ret;
	}
	
	virtual void complete() override {
		Super::complete();
		randomize_constants();
	}
	
	virtual MyHypothesis make_neighbor(int k) const override {
		auto ret = Super::make_neighbor(k);
		ret.randomize_constants();
		return ret;
	}
	virtual void expand_to_neighbor(int k) override {
		Super::expand_to_neighbor(k);
		randomize_constants();		
	}
	
	[[nodiscard]] static MyHypothesis sample() {
		auto ret = Super::sample();
		ret.randomize_constants();
		return ret;
	}
};


////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

// Define these types -- they are needed below
using data_t  = MyHypothesis::data_t;
using datum_t = MyHypothesis::datum_t;

#include "Polynomial.h"
#include "ReservoirSample.h"


data_t mydata;
// keep a big set of samples form structures to the best found for each structure
std::map<std::string,ReservoirSample<MyHypothesis>> overall_samples; 


// useful for extracting the max
std::function posterior = [](const MyHypothesis& h) {return h.posterior; };

/**
 * @brief This trims overall_samples to the top N different structures with the best scores.
 * 			NOTE: This is a bit inefficient because we really could avoid adding structures until they have
 * 		 		a high enough score (as we do in Top) but then if we did find a good sample, we'd
 * 			    have missed everything before...
 */
void trim_overall_samples(size_t N) {
	
	if(overall_samples.size() < N) return;
	
	std::vector<double> structureScores;
	for(auto& m: overall_samples) {
		structureScores.push_back( max_of(m.second.values(), posterior).second );
	}

	// sorts low to high to find the nstructs best's score
	std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
	double cutoff = structureScores[N];
	
	// now go through and trim
	auto it = overall_samples.begin();
	while(it != overall_samples.end()) {
		double b = max_of((*it).second.values(), posterior).second;
		if(b < cutoff) { // if we erase
			it = overall_samples.erase(it);// wow, erase returns a new iterator which is valid
		}
		else {
			++it;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "PartialMCTSNode.h"
#include "MCMCChain.h"

class MyMCTS : public PartialMCTSNode<MyMCTS, MyHypothesis> {
	using Super = PartialMCTSNode<MyMCTS, MyHypothesis>;
	using Super::Super;

public:
	MyMCTS(MyMCTS&&) { assert(false); } // must be defined but not used

	virtual generator<MyHypothesis&> playout(MyHypothesis& current) override {
		// define our own playout here to call our callback and add sample to the MCTS
		
		MyHypothesis h0 = current; // need a copy to change resampling on 
		for(auto& n : h0.get_value() ){
			n.can_resample = false;
		}

		h0.complete(); // fill in any structural gaps	
		
		// check to see if we have no gaps and no resamples, then we just run one sample. 
		std::function no_resamples = +[](const Node& n) -> bool { return not n.can_resample;};
		if(h0.count_constants() == 0 and h0.get_value().all(no_resamples)) {
			h0.compute_posterior(*data);
			co_yield h0;
		}
		else {
			// else we run vanilla MCMC
			MCMCChain chain(h0, data);
			for(auto& h : chain.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart)))  {
				add_sample(h.posterior);
				co_yield h;
			}

		}
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "FleetArgs.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	FleetArgs::inner_timestring = "1m";
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Symbolic regression");
	fleet.add_option("--nsamples", nsamples, "How many samples per structure?");
	fleet.add_option("--nstructs", nstructs, "How many structures?");
	fleet.add_option("--polynomial-degree",   polynomial_degree,   "Defaultly -1 means we store everything, otherwise only keep polynomials <= this bound");
	fleet.initialize(argc, argv);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the data
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	for(auto [xstr, ystr, sdstr] : read_csv<3>(FleetArgs::input_path, '\t')) {
		auto x = std::stod(xstr);
		auto y = std::stod(ystr);
		
		auto k = xstr + ystr + sdstr;
		if(contains(k, " ")) {
			CERR "*** Whitespace error on [" << k << "]" ENDL;
			assert( false && "*** Whitespace is probably not intended? Columns should be tab separated"); // just check for
		}
		
		// process percentages
		double sd; 
		if(sdstr[sdstr.length()-1] == '%') sd = y * std::stod(sdstr.substr(0, sdstr.length()-1)) / 100; // it's a percentage
		else                               sd = std::stod(sdstr);		
		assert(sd > 0.0 && "*** You probably didn't want a zero SD?");
		
		COUT "# Data:\t" << x TAB y TAB sd ENDL;
		
		mydata.push_back( {.input=(double)x, .output=(double)y, .reliability=sdscale*(double)sd} );
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 	// Set up the grammar and hypothesis
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
	MyHypothesis h0; // NOTE: We do NOT want to sample, since that constrains the MCTS 
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Run MCTS
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	MyMCTS m(h0, FleetArgs::explore, &mydata);
	for(auto& h: m.run(Control(), h0) | print(FleetArgs::print, "# ") ) {

		if(h.posterior == -infinity or std::isnan(h.posterior)) continue; // ignore these
		
		// toss non-linear samples here if we request linearity
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			continue;
		
		// It's important here that we do a structure_string without a dot, because
		// we want to store hypotheses in the same place, even if they came from different MCTS nodes
		auto ss = h.structure_string(false); 
		
		if(not overall_samples.count(ss)) { // create this if it doesn't exist
			overall_samples.emplace(ss,nsamples);
		}
		
		// and add it
		overall_samples[ss] << h;
		
		if(overall_samples.size() >= trim_at) {
			trim_overall_samples(trim_to);
		}
	}
	
	// print our trees if we want them
	m.print(h0, FleetArgs::tree_path.c_str());
	
	//------------------
	// Postprocessing
	//------------------
	
	COUT "# Trimming." ENDL;
	
	trim_overall_samples(nstructs);

	// figure out the structure normalizer
	double Z = -infinity;
	for(auto& s: overall_samples) {
		Z = logplusexp(Z, max_of(s.second.values(), posterior).second);
	}
	
	// And display!
	COUT "structure\tstructure.max\tweighted.posterior\tposterior\tprior\tlikelihood\tf0\tf1\tpolynomial.degree\th\tparseable.h" ENDL;
	for(auto& s : overall_samples) {
		
		double best_posterior = max_of(s.second.values(), posterior).second;
		
		// find the normalizer for this structure
		double sz = -infinity;
		for(auto& h : s.second.values()) {
			sz = logplusexp(sz, h.posterior);
		}
		
		for(auto h : s.second.values()) {
			COUT std::setprecision(14) <<
				 QQ(h.structure_string()) TAB 
				 best_posterior TAB 
				 ( (h.posterior-sz) + (best_posterior-Z)) TAB 
				 h.posterior TAB h.prior TAB h.likelihood TAB
				 h.callOne(0.0, NaN) TAB 
				 h.callOne(1.0, NaN) TAB 
				 get_polynomial_degree(h.get_value(), h.constants) TAB 
				 Q(h.string()) TAB 
				 Q(h.serialize()) 
				 ENDL;
		}
	}
	COUT "# **** REMINDER: These are not printed in order! ****" ENDL;
	
	
//	COUT "# MCTS tree size:" TAB m.count() ENDL;	
}
