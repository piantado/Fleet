
// ./main --time=1h --inner-time=5s --explore=0.1 --thin=0 --threads=1 --input=./data-sources/Science/Zipf/data.txt --tree=./out/tree.txt

// TODO: ADD INSERT/DELETE TO PROPOSALS 
// ADD flag for constants vs not 

// Weird, on Feynman dataset, we get 
// III.8.54	89	prob	sin(E_n*t/(h/(2*pi)))**2
// λx.(x0*(x1*sin(x2)))
// But that's only on the first 100 data points -- so maybe we need to sample randomly?

// TODO: Maybe add burn-in??

// Needed when SDs are small, b/c then they can lead to positive likelihoods
#define NO_BREAKOUT 1

#include <cmath>
#include "Random.h"
#include "ConstantContainer.h"

using D = double;

const double sdscale  = 1.0; // can change if we want
size_t nsamples = 100; // 100 // how many per structure?
size_t nstructs = 100; //100 // print out all the samples from the top this many structures
int    polynomial_degree = -1; //-1 means do everything, otherwise store ONLY polynomials less than or equal to this bound

size_t BURN_N = 0; // 1000; // burn this many at the start of each MCMC chain -- probably NOT needed if doing weighted samples

const size_t trim_at = 5000; // when we get this big in overall_sample structures
const size_t trim_to = 1000;  // trim to this size

size_t head_data = 0; // if nonzero, this is the max number of lines we'll read in

double fix_sd = -1; // when -1, we won't fix the SD to any particular value

// these are used for some constant proposals
double data_X_mean = NaN;
double data_Y_mean = NaN;
double data_X_sd   = NaN;
double data_Y_sd   = NaN;

const size_t MAX_VARS = 9; // arguments are at most this many 
size_t       NUM_VARS = 1; // how many predictor variables 

using X_t = std::array<D,MAX_VARS>;

char sep = '\t'; // default input separator

// we also consider our scale variables as powers of 10
const int MIN_SCALE = -3; 
const int MAX_SCALE = 4;

// Proposals and random generation happen on a variety of scales
double random_scale() { return pow(10, myrandom(-4, 5)); }


// Friendly printing but only the first NUM_VARS of them
std::ostream& operator <<(std::ostream& o, X_t a) {
	
	std::string out = "";
	
	for(size_t i=0;i<NUM_VARS;i++) {
		out = out + str(a[i]) + " ";
	}
	out.erase(out.size()-1); // last separator
	
	o << "<"+out+">";
	
	return o;
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Singleton.h"
#include "Grammar.h"


class MyGrammar : public Grammar<X_t,D, D,X_t>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		add("(%s+%s)",    +[](D a, D b) -> D     { return a+b; }),
		add("(%s-%s)",    +[](D a, D b) -> D     { return a-b; }),
		add("(%s*%s)",    +[](D a, D b) -> D     { return a*b; }),
		add("(%s/%s)",    +[](D a, D b) -> D     { return (b==0 ? NaN : a/b); }),
		
		add("pow(%s,%s)",    +[](D a, D b) -> D     { return pow(a,b); }),

//		add("rtp",    +[]()          -> D { return std::sqrt(2*M_PI); });
//		add("sq(%s)",    +[](D a)          -> D { return a*a; }, 1.),
//		add("expm(%s)",    +[](D a)          -> D { return exp(-a); }, 1.),
		
		add("(-%s)",      +[](D a)          -> D { return -a; }),
		add("exp(%s)",    +[](D a)          -> D { return exp(a); }, 1),
		add("log(%s)",    +[](D a)          -> D { return log(a); }, 1),
		
		add("1",          +[]()             -> D { return 1.0; }),
		
		add("0.5",          +[]()           -> D { return 0.5; }),
		add("2",          +[]()             -> D { return 2.0; }),
		add("3",          +[]()             -> D { return 3.0; }),
		add("pi",          +[]()            -> D { return M_PI; }),
		add("sin(%s)",    +[](D a)          -> D { return sin(a); }, 1./3),
		add("cos(%s)",    +[](D a)          -> D { return cos(a); }, 1./3),
		add("asin(%s)",    +[](D a)         -> D { return asin(a); }, 1./3),
		
		// give the type to add and then a vms function
//		add_vms<D>("C", new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
//						
//				// Here we are going to use a little hack -- we actually know that vms->program_loader
//				// is of type MyHypothesis, so we will cast to that
//				auto* h = dynamic_cast<ConstantContainer*>(vms->program.loader);
//				if(h == nullptr) { assert(false); }
//				else {
//					assert(h->constant_idx < h->constants.size()); 
//					vms->template push<D>(h->constants.at(h->constant_idx++));
//				}
//		}), 3.0);
		
		add("x",             Builtins::X<MyGrammar>, 1.0);
		
		// NOTE: Below we must add all the accessors like		
//		add("%s1", 			+[](X_t x) -> D { return x[1]; });
		
	}					  
					  
} grammar;

// check if a rule is constant 
bool isConstant(const Rule* r) { return r->format == "C"; }

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public ConstantContainer,
						   public LOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar> {
	/* This class handles enumeration of the structure and critically does MCMC over the constants */
	
public:

	using Super = LOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar>;
	using Super::Super;

	virtual D callOne(const X_t x, const D err) {
		// We need to override this because LOTHypothesis::callOne asserts that the program is non-empty
		// but actually ours can be if we are only a constant. 
		// my own wrapper that zeros the constant_i counter
		
		constant_idx = 0;
		const auto out = Super::callOne(x,err);
		assert(constant_idx == constants.size()); // just check we used all constants
		return out;
	}
	
	size_t count_constants() const override {
		size_t cnt = 0;
		for(const auto& x : value) {
			cnt += isConstant(x.rule);
		}
		return cnt;
	}
	
	// Propose to a constant c, returning a new value and fb
	// NOTE: When we use a symmetric drift kernel, fb=0
	std::pair<double,double> constant_proposal(double c) const override { 
			
		if(flip(0.5)) {
			return std::make_pair(random_normal(c, random_scale()), 0.0);
		}
		else { 
			
			// one third probability for each of the other choices
			if(flip(0.33)) { 
				auto v = random_normal(data_X_mean, data_X_sd);
				double fb = normal_lpdf(v, data_X_mean, data_X_sd) - 
							normal_lpdf(c, data_X_mean, data_X_sd);
				return std::make_pair(v,fb);
			}
			else if(flip(0.5)) {
				auto v = random_normal(data_Y_mean, data_Y_sd);
				double fb = normal_lpdf(v, data_Y_mean, data_Y_sd) - 
							normal_lpdf(c, data_Y_mean, data_Y_sd);
				return std::make_pair(v,fb);
			}
			else {
				// do nothing
				return std::make_pair(c,0.0);
			}
		}
	}

	double compute_single_likelihood(const datum_t& datum) override {
		double fx = this->callOne(datum.input, NaN);
		
		if(std::isnan(fx) or std::isinf(fx)) 
			return -infinity;
			
		//PRINTN(string(), datum.output, fx, datum.reliability, normal_lpdf( fx, datum.output, datum.reliability ));
		
		return normal_lpdf(fx, datum.output, datum.reliability );		
	}
	
	
	virtual double compute_prior() override {
		
		if(this->value.count() > 16) {
			return this->prior = -infinity;
		}
		
		this->prior = Super::compute_prior() + ConstantContainer::constant_prior();
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
		return  prefix + LAMBDAXDOT_STRING +  __my_string_recurse(&value, idx);
	}
	
	virtual std::string structure_string(bool usedot=true) const {
		return Super::string("", usedot);
	}
	
	/// *****************************************************************************
	/// Change equality to include equality of constants
	/// *****************************************************************************
	
	virtual bool operator==(const MyHypothesis& h) const override {
		// equality requires our constants to be equal 
		return this->Super::operator==(h) and ConstantContainer::operator==(h);
	}

	virtual size_t hash() const override {
		// hash includes constants so they are only ever equal if constants are equal
		size_t h = Super::hash();
		hash_combine(h, ConstantContainer::hash());
		return h;
	}
	
	/// *****************************************************************************
	/// Implement MCMC moves as changes to constants
	/// *****************************************************************************
	
	virtual std::pair<MyHypothesis,double> propose() const override {
		// Our proposals will either be to constants, or entirely from the prior
		// Note that if we have no constants, we will always do prior proposals
//		PRINTN("\nProposing from\t\t", string());
		
		auto NC = count_constants();
		
		if(NC > 0 and flip(0.80)){
			MyHypothesis ret = *this;
			
			double fb = 0.0; 
			
			// now add to all that I have
			for(size_t i=0;i<NC;i++) { 
				auto [v, __fb] = constant_proposal(constants[i]);
				ret.constants[i] = v;
				fb += __fb;
			}
			
//			PRINTN("Constant Proposal\t", ret.string());
			return std::make_pair(ret, fb);
		}
		else {
			
			// else we could just propose from Super and then randomize, but we actually want to 
			// but we actually want to do insert/delete
			//auto [ret, fb] = Super::propose(); // a proposal to structure
			
			std::pair<Node,double> x;

			if(flip(0.5))       x = Proposals::regenerate(&grammar, value);	
			else if(flip(0.1))  x = Proposals::sample_function_leaving_args(&grammar, value);
			else if(flip(0.1))  x = Proposals::swap_args(&grammar, value);
			else if(flip())     x = Proposals::insert_tree(&grammar, value);	
			else                x = Proposals::delete_tree(&grammar, value);			
			
			MyHypothesis ret{std::move(x.first)};
			ret.randomize_constants(); // with random constants -- this resizes so that it's right for propose

			//			PRINTN("Structural proposal\t", structure_string(), ret.string());
			
			return std::make_pair(ret, 
								  x.second + ret.constant_prior()-this->constant_prior()); 
		}
			
	}
		
	virtual MyHypothesis restart() const override {
		MyHypothesis ret = Super::restart(); // reset my structure
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

#include "Polynomial.h"
#include "ReservoirSample.h"

MyHypothesis::data_t mydata;

// keep a big set of samples form structures to the best found for each structure
std::map<std::string,PosteriorWeightedReservoirSample<MyHypothesis>> overall_samples; 

// useful for extracting the max
std::function posterior = [](const MyHypothesis& h) {return h.posterior; };

double max_posterior(const PosteriorWeightedReservoirSample<MyHypothesis>& rs) {
	return max_of(rs.values(), posterior).second;
}

/**
 * @brief This trims overall_samples to the top N different structures with the best scores.
 * 			NOTE: This is a bit inefficient because we really could avoid adding structures until they have
 * 		 		a high enough score (as we do in Top) but then if we did find a good sample, we'd
 * 			    have missed everything before...
 */
void trim_overall_samples(const size_t N) {
	
	if(overall_samples.size() < N or N <= 0) return;
	
	std::vector<double> structureScores;
	for(auto& [ss, R] : overall_samples) {
		structureScores.push_back( max_posterior(R) );
	}

	// sorts low to high to find the nstructs best's score
	std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
	double cutoff = structureScores[N-1];
	
	// now go through and trim
	auto it = overall_samples.begin();
	while(it != overall_samples.end()) {
		double b = max_posterior(it->second);
		if(b < cutoff) { // if we erase
			it = overall_samples.erase(it); // wow, erases returns a new iterator which is valid
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
			double best_posterior = -infinity; 
			long steps_since_best = 0;
			
			// else we run vanilla MCMC
			MCMCChain chain(h0, data);
			for(auto& h : chain.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart)) | burn(BURN_N) | thin(FleetArgs::inner_thin) ) {
				this->add_sample(h.posterior);
				
				co_yield h;

				// return if we haven't improved in howevermany samples. 
				if(h.posterior > best_posterior) {
					best_posterior = h.posterior;
					steps_since_best = 0;
				}
				
				//
				if(steps_since_best > 100000) break; 
			}

		}
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "FleetArgs.h"
#include "Fleet.h" 
#include "ParallelTempering.h"

#ifndef DO_NOT_INCLUDE_MAIN

int main(int argc, char** argv){ 
	
	std::string strsep = "\t";
	
	FleetArgs::inner_timestring = "1m"; // default inner time
//	FleetArgs::inner_restart = 2500; // set this as the default 
	int space_sep=0;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Symbolic regression");
	fleet.add_option("--nvars", NUM_VARS, "How many predictor variables are there?");
	fleet.add_option("--head-data", head_data, "Only take this many lines of data (default: 0 means all)");
	fleet.add_option("--fix-sd", fix_sd, "Should we force the sd to have a particular value?");
	fleet.add_option("--nsamples", nsamples, "How many samples per structure?");
	fleet.add_option("--nstructs", nstructs, "How many structures?");
	fleet.add_option("--space", space_sep, "If 1, our data is space-separated");
	fleet.add_option("--polynomial-degree",   polynomial_degree,   "Defaultly -1 means we store everything, otherwise only keep polynomials <= this bound");
	fleet.add_option("--sep", strsep, "Separator for input data (usually space or tab)");
	fleet.initialize(argc, argv);
	
	assert(strsep.length()==1);
	sep = strsep.at(0);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the accessors of the variables
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	for(size_t i=0;i<NUM_VARS;i++){
		std::function fi = [=](X_t x) -> D { return x[i]; };
		grammar.add("%s"+str(i), fi, 8.0); // hmm 5 each? Or total?
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the data
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
	// store the x and y values so we can do constant proposals
	std::vector<double> data_x;
	std::vector<double> data_y;
	
//	for(auto [xstr, ystr, sdstr] : read_csv<3>(FleetArgs::input_path, false, '\t')) {
	for(auto v : read_csv(FleetArgs::input_path, false, sep)) { // NOTE: MUST BE \t for our data!!
		
		if(fix_sd == -1) assert(v.size() == NUM_VARS + 2); // must have sd
		else        	 assert(v.size() >= NUM_VARS + 1); // may have sd
		
		X_t x;
		
		size_t i=0;
		for(;i<NUM_VARS;i++) {
			x[i] = string_to<D>(v[i]);
			assert(not contains(v[i]," "));
			
			data_x.push_back(x[i]); // all are just counted here in the mean -- maybe not a great idea
		}
		assert(not contains(v[i]," "));
		auto y = string_to<D>(v[i]); i++; 
		assert(not contains(v[i]," "));
		auto sdstr = v[i]; // processed below
		
		// process percentages
		double sd; 
		if(fix_sd != -1) {
			sd = fix_sd;
		}
		else {
			// process the line 
			if(sdstr[sdstr.length()-1] == '%') sd = y * std::stod(sdstr.substr(0, sdstr.length()-1)) / 100.0; // it's a percentage
			else                               sd = std::stod(sdstr);								
		}
				
		assert(sd > 0.0 && "*** You probably didn't want a zero SD?");
		
//		COUT "# Data:\t" << x TAB y TAB sd ENDL;
		
		data_y.push_back(y);
		
		mydata.emplace_back(x,y,sdscale*(double)sd);
		
		if(mydata.size() == head_data) break;
	}
	
	data_X_mean = mean(data_x);
	data_Y_mean = mean(data_y);
	data_X_sd   = mean(data_x);
	data_Y_sd   = mean(data_y);
	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Run MCTS
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	TopN<MyHypothesis> best(1);
	best.print_best = true;
	
/*
	//	auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:\u25A0;0:\u25A0");
	//	auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:(%s/%s);0:\u25A0;0:2;0:pow(%s,%s);0:\u25A0;0:\u25A0");
//		auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:(%s/%s);0:pow(%s,%s);0:\u25A0;0:2;0:2;0:pow(%s,%s);0:\u25A0;0:0.5");
					
		auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:\u25A0;0:pow(%s,%s);0:\u25A0;0:\u25A0;0:2;0:pow(%s,%s);0:\u25A0;0:\u25A0");

		for(auto& n : s) { n.can_resample=false; }
		
		grammar.complete(s);
		
		{
			MyHypothesis h0(s);
			ParallelTempering m(h0, &mydata, 5, 1.1);
			for(auto& h: m.run(Control()) | best | print(FleetArgs::print, "# ")  ) {
			}
		}	
		
		best.print("# Overall best: "+best.best().structure_string()+"\t");
		return 0;
*/
	
	MyHypothesis h0; // NOTE: We do NOT want to sample, since that constrains the MCTS 
	MyMCTS m(h0, FleetArgs::explore, &mydata);
	for(auto& h: m.run(Control(), h0) | print(FleetArgs::print, "# ")  ) {
	
//	auto h0 = MyHypothesis::sample(); // NOTE: We do NOT want to sample, since that constrains the MCTS 	
//	ParallelTempering m(h0, &mydata, FleetArgs::nchains, 2.20);
//	for(auto& h: m.run(Control()) | print(FleetArgs::print, "# ")  ) {
			
		if(h.posterior == -infinity or std::isnan(h.posterior)) continue; // ignore these
		
		// toss non-linear samples here if we request linearity
		// and non-polynomials (NOTE: This is why we have "not" in the next line
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			continue;
		
		best << h;
		
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
	for(auto& [ss,R]: overall_samples) {
		Z = logplusexp(Z, max_posterior(R));
	}
	
	// sort the keys into overall_samples by highest posterior so we print out in order
	std::vector<std::pair<double,std::string>> K;
	for(auto& [ss,R] : overall_samples) {
		K.emplace_back(max_posterior(R), ss);
	}
	std::sort(K.begin(), K.end());
	
	// And display!
	COUT "structure\tstructure.max\tweighted.posterior\tposterior\tprior\tlikelihood\tf0\tf1\tpolynomial.degree\th" ENDL;//\tparseable.h" ENDL;
	for(auto& k : K) {
		double best_posterior = k.first;
		auto& ss = k.second; 
		auto& R = overall_samples[ss]; // the reservoir sample for ss
			
		// find the normalizer for this structure
		double sz = -infinity;
		for(auto& h : R.values()) {
			sz = logplusexp(sz, h.posterior);
		}
		
		X_t ones;  ones.fill(1.0);
		X_t zeros; zeros.fill(0.0);
		
		for(auto h : R.values()) {
			COUT std::setprecision(14) <<
				 QQ(h.structure_string()) TAB 
				 best_posterior TAB 
				 ( (h.posterior-sz) + (best_posterior-Z)) TAB 
				 h.posterior TAB h.prior TAB h.likelihood TAB
				 h.callOne(ones, NaN) TAB 
				 h.callOne(zeros, NaN) TAB 
				 get_polynomial_degree(h.get_value(), h.constants) TAB 
				 Q(h.string()) // TAB 
				 //Q(h.serialize()) 
				 ENDL;
		}
	}
	
//	auto b = best.best();
//	for(auto& d : mydata) {
//		PRINTN(d.input, b.callOne(d.input, NaN), d.output, d.reliability);
//	}
	
	best.print("# Overall best: "+best.best().structure_string()+"\t");
	PRINTN("# Overall samples size:" , overall_samples.size());
//	PRINTN("# MCTS tree size:", m.count());	
}

#endif 
