

///########################################################################################
// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second on 4 threads
///########################################################################################


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the object features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

enum    class  Shape  { Square, Triangle, Circle};
enum    class  Color  { Red, Green, Blue};
typedef struct { Color color; Shape shape; } Object;

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }, 2.0), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	// that + is really insane, but is needed to convert a lambda to a function pointer

	Primitive("red(%s)",       +[](Object x)       -> bool { return x.color == Color::Red; }),
	Primitive("green(%s)",     +[](Object x)       -> bool { return x.color == Color::Green; }),
	Primitive("blue(%s)",      +[](Object x)       -> bool { return x.color == Color::Blue; }),

	Primitive("square(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Square; }),
	Primitive("triangle(%s)",  +[](Object x)       -> bool { return x.shape == Shape::Triangle; }),
	Primitive("circle(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Circle; }),
	
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<Object>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,Object> {
	using Super =  Grammar<bool,Object>;
	using Super::Super;
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,Object,bool,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,Object,bool,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability x.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& x) override {
		bool out = callOne(x.input, false);
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename Grammar_t>
void checkNode(const Grammar_t* g, const Node& n) {
	
	// first check the iterator hits all the nodes:
	// NOTE: this works because count does not use the iterator
	size_t cnt = 0;
	for(auto& ni : n) {
		UNUSED(ni);
		cnt++;	
	}
	assert(cnt == n.count());
	
	// check the parent references
	for(auto& ni: n) {
		ni.check_child_info();
	}


	// check that each rule is recovered correctly
//	for(auto ni : n) {
//		assert(g.get_rule(ni.nt, ni.rule->format)
//	}

	// check the log probability that I get out. 
	
}


template<typename Grammar_t, typename Hypothesis_t>
void checkLOTHypothesis(const Grammar_t* g, const Hypothesis_t h){
	checkNode(g, h.value);
		
	// Check that copies and things work right:
	decltype(h) newH = h;
	checkNode(g, newH.value);
	assert(newH == h);
	assert(newH.value == h.value);
	assert(newH.posterior == h.posterior);
	assert(newH.prior == h.prior);
	assert(newH.likelihood == h.likelihood);
	assert(newH.posterior == newH.prior + newH.likelihood);
	assert(newH.hash() == h.hash());	
//	assert(newH.prior == g.log_probability(newH.value));
}

/**
 * @brief Check the stuff stored in a TopN. 
 * @param g - the grammar
 * @param top
 * @param check_counts -- should I check that the probabilities and counts are what they should be? 
 */

template<typename Grammar_t, typename Top_t>
void checkTop(const Grammar_t* g, const Top_t& top, const bool check_counts=false) {
	
	// check each hypothesis -- make sure it copies correctly
	for(auto& h : top.values()) {
		checkLOTHypothesis(g, h);
		assert(h.posterior > -infinity);
		assert(not std::isnan(h.posterior));
	}	
	
	// check that w have sorted and extracted the best and worst
	double best = top.best().posterior;
	double worst = top.worst().posterior;
	double lower_lp = -infinity;
	for(auto& h : top.values()) {
		if(lower_lp > infinity) {
			assert(h.posterior >= lower_lp);
		}
		lower_lp = h.posterior; 
		
		assert(h.posterior >= worst);
		assert(h.posterior <= best);
	}
	
	if(check_counts) {
		
		
	}
}


/**
 * @brief Given two top distributions, how different are they? For now, this is the (non-log) probability mass of 
 *        (stuff in X but not Y plus stuff in Y but not X)/2
 * @param x
 * @param y
 * @return 
 */
template<typename Top_t>
double top_difference(Top_t& x, Top_t& y) {
	double xZ = x.Z();
	double xMy = 0.0;
	for(const auto& h : x.values()) {
		if(not y.contains(h)) {
			xMy += exp(h.posterior-xZ);
		}
	}
	
	double yZ = y.Z();
	double yMx = 0.0;
	for(const auto& h : y.values()) {
		if(not x.contains(h)) {
			yMx += exp(h.posterior-yZ);
		}
	}
	return (xMy + yMx)/2.0;
}


#include "MCMCChain.h"
#include "ParallelTempering.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Testing");
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)	
	MyGrammar grammar(PRIMITIVES);
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	// initial hypothesis
	MyHypothesis h0(&grammar);
	
	//------------------
	// set up the data
	//------------------
	
	mydata.push_back(   (MyHypothesis::datum_t){ (Object){Color::Red, Shape::Triangle}, true,  0.75 }  );
	mydata.push_back(   (MyHypothesis::datum_t){ (Object){Color::Red, Shape::Square},   false, 0.75 }  );
	mydata.push_back(   (MyHypothesis::datum_t){ (Object){Color::Red, Shape::Square},   false, 0.75 }  );
	
	const size_t N = 100; // check equality on the top this many
	
	//------------------
	// Actually run
	//------------------
		
	COUT "# MCMC..." ENDL;
	Fleet::Statistics::TopN<MyHypothesis> top_mcmc(N);
	h0 = h0.restart();
	MCMCChain chain(h0, &mydata, top_mcmc);
	chain.run(Control(mcmc_steps,runtime));
	checkTop(&grammar, top_mcmc);
	
	COUT "# Parallel Tempering..." ENDL;
	Fleet::Statistics::TopN<MyHypothesis> top_tempering(N);
	h0 = h0.restart();
	ParallelTempering samp(h0, &mydata, top_tempering, 8, 1000.0, false);
	samp.run(Control(mcmc_steps, runtime, nthreads), 500, 1000);	// we run here with fast swaps, adaptation to fit more in 
 	// NOTE: Running ParallelTempering with allcallback (default) will try to put
	// *everything* into top, which means that the counts you get will no longer 
	// be samples (and in fact should be biased towards high-prior hypotheses)	
	checkTop(&grammar, top_tempering);
	
	CERR top_difference(top_mcmc, top_tempering) ENDL;
	
//
//
//	COUT "# Enumerating...." ENDL;
//	Fleet::Statistics::TopN<MyHypothesis> top_enumerate(N);
//	for(enumerationidx_t z=1;z<1000 and !CTRL_C;z++) {
//		auto n = grammar.expand_from_integer(0, z);
//		checkNode(&grammar, n);
//	
//		MyHypothesis h(&grammar);
//		h.set_value(n);
//		h.compute_posterior(mydata);
//		
//		top_enumerate << h;
//		
//		// check our enumeration order
//		auto o  = grammar.compute_enumeration_order(n);
//		assert(o == z); // check our enumeration order
//	}
//	checkTop(&grammar, top_enumerate);

	COUT "# PriorSampling...." ENDL;
	Fleet::Statistics::TopN<MyHypothesis> top_generate(N);
	for(enumerationidx_t z=0;z<1000 and !CTRL_C;z++) {
		auto n = grammar.generate<bool>();
		checkNode(&grammar, n);
		
		MyHypothesis h(&grammar);
		h.set_value(n);
		h.compute_posterior(mydata);
		
		top_generate << h;
	}
	checkTop(&grammar, top_generate);
	
	/*
	 * What we want to check:
	 * 	- each tree matches its probability
	 *  - each node enumeration counts correctly
	 *  - check multicore performance 
	 *  - Check evaluation? Or maybe that's captured by ensuring the posterior is right?
	 *  - check on numerics -- logsumexp
	*/
	

	

	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;

}