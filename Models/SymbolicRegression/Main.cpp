// TODO: Check that we never put a nan in a map because that's not alloewd!
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
				auto* h = dynamic_cast<ConstantContainer*>(vms->program_loader);
				if(h == nullptr) { assert(false); }
				else {
					assert(h->constant_idx < h->constants.size()); 
					vms->template push<D>(h->constants.at(h->constant_idx++));
				}
		}), 5.0);
							
		add("x",             Builtins::X<MyGrammar>, 5.0);
	}
					  
					  
};

// check if a rule is constant
bool isConstant(const Rule* r) { return r->format == "C"; }


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public ConstantContainer,
						   public LOTHypothesis<MyHypothesis,D,D,MyGrammar> {
	/* This class handles enumeration of the structure and critically does MCMC over the constants */
	
public:

	using Super = LOTHypothesis<MyHypothesis,D,D,MyGrammar>;
	using Super::Super;

	virtual D callOne(const D x, const D err) {
		// my own wrapper that zeros the constant_i counter
		constant_idx = 0;
		auto out = Super::callOne(x,err);
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
		return this->prior = Super::compute_prior() + compute_constants_prior();
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
			
			ret.randomize_constants(); // with random constants
			
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
};


////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

// Define these types -- they are needed below
using data_t  = MyHypothesis::data_t;
using datum_t = MyHypothesis::datum_t;

#include "Polynomial.h"
#include "ReservoirSample.h"

// keep a big set of samples form structures to the best found for each structure
std::map<std::string,ReservoirSample<MyHypothesis>> overall_samples; 
std::mutex overall_sample_lock;

data_t mydata;

// useful for extracting the max
std::function posterior = [](const MyHypothesis& h) {return h.posterior; };

/**
 * @brief This trims overall_samples to the top N different structures with the best scores.
 * 			NOTE: This is a bit inefficient because we really could avoid adding structures until they have
 * 		 		a high enough score (as we do in Top) but then if we did find a good sample, we'd
 * 			    have missed everything before...
 */
void trim_overall_samples(size_t N) {
	
	//std::lock_guard guard(overall_sample_lock); // This is called in myCallback so we can't lock in here
	
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

#include "MCTS.h"
#include "MCMCChain.h"

/**
 * @class MyCallback
 * @author piantado
 * @date 03/07/20
 * @file Main.cpp
 * @brief A callback here for processing samples, putting them into the right places
 */
void myCallback(MyHypothesis& h) {
		if(h.posterior == -infinity or std::isnan(h.posterior)) return; // ignore these
		
		// toss non-linear samples here if we request linearity
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			return;
		
		// It's important here that we do a structure_string without a dot, because
		// we want to store hypotheses in the same place, even if they came from different MCTS nodes
		auto ss = h.structure_string(false); 
		
		std::lock_guard guard(overall_sample_lock);
		if(!overall_samples.count(ss)) { // create this if it doesn't exist
			overall_samples.emplace(ss,nsamples);
		}
		
		// and add it
		overall_samples[ss] << h;
		
		if(overall_samples.size() >= trim_at) {
			trim_overall_samples(trim_to);
		}
}


class MyMCTS : public PartialMCTSNode<MyMCTS, MyHypothesis, decltype(myCallback) > {
	using Super = PartialMCTSNode<MyMCTS, MyHypothesis, decltype(myCallback)>;
	using Super::Super;

public:
	MyMCTS(MyMCTS&&) { assert(false); } // must be defined but not used

	virtual void playout(MyHypothesis& current) override {
		// define our own playout here to call our callback and add sample to the MCTS
		
		MyHypothesis h0 = current; // need a copy to change resampling on 
		for(auto& n : h0.get_value() ){
			n.can_resample = false;
		}
		h0.complete(); // fill in any structural gaps
		
		// make a wrapper to add samples
		std::function cb = [&](MyHypothesis& h) { 
			myCallback(h);
			// and update MCTS -- here every sample
			add_sample(h.posterior);
		};		
		
		// NOTE That we might run this when there are no constants. 
		// This is hard to avoid if we want to allow restarts, since those occur within MCMCChain
		MCMCChain chain(h0, data, cb);
		chain.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, inner_restarts)); // run mcmc with restarts; we sure shouldn't run more than runtime
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
	fleet.add_option("--inner-restart",   inner_restarts,   "When does the inner MCMC chain restart?");	
	fleet.initialize(argc, argv);
	
	// set up the data
	for(auto [xstr, ystr, sdstr] : read_csv<3>(FleetArgs::input_path, '\t')) {
		auto x = std::stod(xstr);
		auto y = std::stod(xstr);
		
		// process percentages
		double sd; 
		if(sdstr[sdstr.length()-1] == '%') sd = y * std::stod(sdstr.substr(0, sdstr.length()-1));
		else                               sd = std::stod(sdstr);
		
		mydata.push_back( {.input=(double)x, .output=(double)y, .reliability=sdscale*(double)sd} );
	}
	
 	// Set up the grammar
	MyGrammar grammar;
	
	MyHypothesis h0(&grammar);
	MyMCTS m(h0, FleetArgs::explore, &mydata, myCallback);
	tic();
	m.run(Control(), h0);
	tic();
	
	COUT "# Printing trees." ENDL;
	
	m.print(h0, FleetArgs::tree_path.c_str());

	// set up a paralle tempering object
//	auto h0 = MyHypothesis::make(&grammar);
//	ParallelTempering<MyHypothesis> samp(h0, &mydata, myCallback, 8, 1000.0, false);
//	tic();
//	samp.run(mcmc_steps, runtime, .25, 3.00); 
//	tic();

//	auto h0 = MyHypothesis::make(&grammar);
//	MCMCChain samp(h0, &mydata, cb);
//	tic();
//	samp.run(Control(mcts_steps, runtime)); //30000);		
//	tic();

	
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
				 Q(h.parseable()) 
				 ENDL;
		}
	}
	COUT "# **** REMINDER: These are not printed in order! ****" ENDL;
	
	
	COUT "# MCTS tree size:" TAB m.count() ENDL;	
}
