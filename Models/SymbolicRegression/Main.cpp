#include <cmath>

using D = double;

const double sdscale = 1.0; // can change if we want
const size_t nsamples = 250; // how many per structure?
const size_t nstructs = 50; // print out all the samples from the top this many structures

const size_t trim_at = 2000; // when we get this big in overall_sample structures
const size_t trim_to = 500;  // trim to this size

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<D> {
	using Super=Grammar<D>;
	using Super::Super;
};

// check if a rule is constant
bool isConstant(const Rule* r) { return r->format == "C"; }


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,D,D,MyGrammar> {
	/* This class handles enumeration of the structure and critically does MCMC over the constants */
	
public:
	std::vector<D> constants;
	size_t         constant_idx; // in evaluation, this variable stores what constant we are in 
	
	using Super = LOTHypothesis<MyHypothesis,D,D,MyGrammar>;
	using Super::Super;

	virtual D callOne(const D x, const D err) {
		// my own wrapper that zeros the constant_i counter
		constant_idx = 0;
		auto out = Super::callOne<MyHypothesis>(x,err,this);
		assert(constant_idx == constants.size()); // just check we used all constants
		return out;
	}

	double compute_single_likelihood(const datum_t& datum) override {
		double fx = this->callOne(datum.input, NaN);
		if(std::isnan(fx)) return -infinity;
            
		return normal_lpdf( (fx-datum.output)/datum.reliability );		
	}
	
	size_t count_constants() const {
		size_t cnt=0;
		for(const auto& x : value) {
			cnt += isConstant(x.rule);
		}
		return cnt;
	}
	
	virtual double compute_constants_prior() const {
		double lp = 0.0;
		for(auto& c : constants) {
			lp += cauchy_lpdf(c);
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
			return "("+str(constants[idx++])+")";
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
//			for(int i=(int)n->rule->N-1;i>=0;i--) {
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
	
	virtual std::string structure_string() const {
		return Super::string();
	}
	
	/// *****************************************************************************
	/// Change equality to include equality of constants
	/// *****************************************************************************
	
	virtual bool operator==(const MyHypothesis& h) const override {
		// equality requires our constants to be equal 
		if(! this->Super::operator==(h) ) return false;
		
		assert(constants.size() == h.constants.size()); // has to be if we passed the previous test
		for(size_t i=0;i<constants.size();i++){
			if(constants[i] != h.constants[i]) return false;
		}
		return true;
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
		
		if(flip()){
			MyHypothesis ret = *this;
			
			// now add to all that I have
			for(size_t i=0;i<ret.constants.size();i++) { 
				ret.constants[i] += 0.1*random_cauchy(); // symmetric, not counting in fb
			}
			return std::make_pair(ret, 0.0);
		}
		else {
			auto [h, fb] = Super::propose(); // a proposal to structure
			
			h.randomize_constants(); // with random constants
			
			return std::make_pair(h, fb + h.compute_constants_prior() - this->compute_constants_prior());
		}
	}
	
	virtual void randomize_constants() {
		constants.resize(count_constants());
		for(size_t i=0;i<constants.size();i++) {
			constants[i] = random_cauchy();
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

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define primitives -- these must come after MyHypothesis since 
/// we have to define a custom function that depends on them
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"

std::tuple PRIMITIVES = {
	Primitive("(%s+%s)",    +[](D a, D b) -> D     { return a+b; }),
	Primitive("(%s-%s)",    +[](D a, D b) -> D     { return a-b; }),
	Primitive("(%s*%s)",    +[](D a, D b) -> D     { return a*b; }),
	Primitive("(%s/%s)",    +[](D a, D b) -> D     { return (b==0 ? 0 : a/b); }),
	Primitive("(%s^%s)",    +[](D a, D b) -> D     { return pow(a,b); }),
	Primitive("(-%s)",      +[](D a)          -> D { return -a; }),
	Primitive("exp(%s)",    +[](D a)          -> D { return exp(a); }),
	Primitive("log(%s)",    +[](D a)          -> D { return log(a); }),
	
	Primitive("1",          +[]()             -> D { return 1.0; }),
	
	
	Primitive("C", +[]() -> D {return 0.0;}, 
				   +[](VirtualMachineState<D,D,D>* vms, VirtualMachinePool<VirtualMachineState<D,D,D>>* pool, MyHypothesis* h) -> vmstatus_t {
						assert(h->constant_idx < h->constants.size()); 
						vms->push(h->constants.at(h->constant_idx++));
						return vmstatus_t::GOOD;
				   }, 5.0),
				   
	Builtin::X<D>("x", 5.0)
};


////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

// Define these types -- they are needed below
using data_t  = MyHypothesis::data_t;
using datum_t = MyHypothesis::datum_t;

#include "Data.h"
#include "Polynomial.h"
#include "ReservoirSample.h"

// keep a big set of samples form structures to the best found for each structure
std::map<std::string,ReservoirSample<MyHypothesis>> overall_samples; 
std::mutex overall_sample_lock;

size_t innertime;
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
	
	std::lock_guard guard(overall_sample_lock);
	
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
			it = overall_samples.erase(it);// wow erase returns a new iterator which is valid
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

		auto ss = h.structure_string();
		
		std::lock_guard guard(overall_sample_lock);
		if(!overall_samples.count(ss)) { // create this if it doesn't exist
			overall_samples.emplace(ss,nsamples);
		}
		
		if(overall_samples.size() >= trim_at) {
			trim_overall_samples(trim_to);
		}
		
		// and add it
		overall_samples[ss] << h;
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
		chain.run(Control(0, innertime, 1, 10000)); // run mcmc with restarts; we sure shouldn't run more than runtime
			
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	std::string innertimestr = "1m";
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Symbolic regression");
	fleet.add_option("-I,--inner-time", innertimestr, "Alphabet we will use"); 	// add my own args
	fleet.initialize(argc, argv);
	
	innertime = convert_time(innertimestr);

	// Set up the grammar
	MyGrammar grammar(PRIMITIVES);
	
	// set up the data
	mydata = load_data_file(FleetArgs::input_path.c_str()); 
 
 	//------------------
	// Run
	//------------------
 
	MyHypothesis h0(&grammar);
	MyMCTS m(h0, FleetArgs::explore, &mydata, myCallback);
	tic();
	m.run(Control(), h0);
	tic();
	
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

	trim_overall_samples(nstructs);

	// figure out the structure normalizer
	double Z = -infinity;
	for(auto& m: overall_samples) {
		Z = logplusexp(Z, max_of(m.second.values(), posterior).second);
	}
	
	// And display!
	COUT "structure\tstructure.max\testimated.posterior\tposterior\tprior\tlikelihood\tf0\tf1\tpolynomial.degree\th\tparseable.h" ENDL;
	for(auto& m : overall_samples) {
		
		double best_posterior = max_of(m.second.values(), posterior).second;
		
		// find the normalizer for this structure
		double sz = -infinity;
		for(auto& h : m.second.values()) {
			sz = logplusexp(sz, h.posterior);
		}
		
		for(auto h : m.second.values()) {
			COUT QQ(h.structure_string()) TAB 
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
