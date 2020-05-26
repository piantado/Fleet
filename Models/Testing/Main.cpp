#include <string>
#include <map>

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

std::map<std::string, double> probs = {
										{"and", 1.82}, {"or", 0.11}, {"not", 1.07},
										{"red", 2.4},  {"green", 0.2}, {"blue", 1.4},
										{"square", 5.1}, {"triangle", 4.5}, {"circle", 2.49}
									   };
									   
double zprob() {
	double z = 0.0;
	for(auto& i : probs) {
		z += i.second;
	}
	return z;
}

std::tuple PRIMITIVES = {
	// We've set some arbitrary weights here -- be sure you change them below
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }, probs["and"]), 
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); },  probs["or"]),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); },   probs["not"]),
	
	Primitive("red(%s)",       +[](Object x)       -> bool { return x.color == Color::Red; }, probs["red"]),
	Primitive("green(%s)",     +[](Object x)       -> bool { return x.color == Color::Green; }, probs["green"]),
	Primitive("blue(%s)",      +[](Object x)       -> bool { return x.color == Color::Blue; }, probs["blue"]),

	Primitive("square(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Square; }, probs["square"]),
	Primitive("triangle(%s)",  +[](Object x)       -> bool { return x.shape == Shape::Triangle; }, probs["triangle"]),
	Primitive("circle(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Circle; }, probs["circle"]),
	
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<Object>("x", 41.11) /// different type so shouldn't matter
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

	// check the log probability that I get out. 
	// by counting how things are mapped to strings
	// NOTE: This is specific to this grammar
	std::string s = n.string();
	double mylp = 0.0;
	double lz = log(zprob()); // TODO: This should only be computed once...
	for(auto& i : probs) { 
		mylp += count(s, i.first) * (log(i.second) - lz); // TODO: 
	}
	//CERR mylp TAB g->log_probability(n) TAB n ENDL;
	assert(abs(mylp-g->log_probability(n)) < 0.00001);
}


template<typename Grammar_t, typename Hypothesis_t>
void checkLOTHypothesis(const Grammar_t* g, const Hypothesis_t h){
	checkNode(g, h.get_value());
		
	// Check that copies and things work right:
	decltype(h) newH = h;
	checkNode(g, newH.get_value());
	assert(newH == h);
	assert(newH.get_value() == h.get_value());
	assert(newH.posterior == h.posterior);
	assert(newH.prior == h.prior);
	assert(newH.likelihood == h.likelihood);
	assert(newH.posterior == newH.prior + newH.likelihood);
	assert(newH.hash() == h.hash());	
}

/**
 * @brief Check the stuff stored in a TopN. 
 * @param g - the grammar
 * @param top
 */

template<typename Grammar_t, typename Top_t>
void checkTop(const Grammar_t* g, const Top_t& top) {
	
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


#include "Top.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Testing");
	fleet.initialize(argc, argv); // must happen afer args are processed since the alphabet is in the grammar
	
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
	
	const size_t N = 100000; // check equality on the top this many
	
	//------------------
	// Actually run
	//------------------
		
	COUT "# MCMC..." ENDL;
	TopN<MyHypothesis> top_mcmc(N);
	h0 = h0.restart();
	MCMCChain chain(h0, &mydata, top_mcmc);
	chain.run(Control(mcmc_steps,runtime));
	checkTop(&grammar, top_mcmc);
	assert(not top_mcmc.empty());
	
	// just check out copying
	TopN<MyHypothesis> top_mcmc_copy = top_mcmc;
	checkTop(&grammar, top_mcmc_copy);
	assert(top_difference(top_mcmc, top_mcmc_copy)==0.0);
	assert(not top_mcmc_copy.empty());
	
	// check moving
	TopN<MyHypothesis> top_mcmc_mv = std::move(top_mcmc_copy);
	checkTop(&grammar, top_mcmc_mv);
	assert(top_difference(top_mcmc, top_mcmc_mv)==0.0);
	assert(not top_mcmc_mv.empty());
	
	
	COUT "# Parallel Tempering..." ENDL;
	TopN<MyHypothesis> top_tempering(N);
	h0 = h0.restart();
	ParallelTempering samp(h0, &mydata, top_tempering, 8, 1000.0, false);
	samp.run(Control(mcmc_steps, runtime, nthreads), 500, 1000);	// we run here with fast swaps, adaptation to fit more in 
 	// NOTE: Running ParallelTempering with allcallback (default) will try to put
	// *everything* into top, which means that the counts you get will no longer 
	// be samples (and in fact should be biased towards high-prior hypotheses)	
	checkTop(&grammar, top_tempering);
	assert(not top_tempering.empty());
	
	COUT "# top_difference(top_mcmc, top_tempering) = " << top_difference(top_mcmc, top_tempering) ENDL;

//	COUT "# Enumerating...." ENDL;
//	TopN<MyHypothesis> top_enumerate(N);
//	for(enumerationidx_t z=1;z<1000 and !CTRL_C;z++) {
//		auto n = grammar.expand_from_integer(grammar->nt<bool>(), z);
//		checkNode(&grammar, n);
//		CERR n ENDL;
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
//	assert(not top_enumerate.empty());
//	checkTop(&grammar, top_enumerate);
	
	COUT "# PriorSampling...." ENDL;
	TopN<MyHypothesis> top_generate(N);
	for(enumerationidx_t z=0;z<1000 and !CTRL_C;z++) {
		auto n = grammar.generate<bool>();
		checkNode(&grammar, n);
		
		MyHypothesis h(&grammar);
		h.set_value(n);
		h.compute_posterior(mydata);
		
		top_generate << h;
	}
	checkTop(&grammar, top_generate);
	assert(not top_generate.empty());
	
	COUT "# top_difference(top_mcmc, top_generate) = " << top_difference(top_mcmc, top_generate) ENDL;
	
	/*
	
	 * What we want to check:
	 * 	- each tree matches its probability
	 *  - each node enumeration counts correctly
	 *  - check multicore performance 
	 *  - Check evaluation? Or maybe that's captured by ensuring the posterior is right?
	 *  - check on numerics -- logsumexp
	 *  - check iterator for Nodes -- let's push everything into a pointer structure and be sure we get them all 
	 *  - Check that parseable is parseable..
	*/
	
//	paste(as.character(-exp(rnorm(10))), collapse=",")
	std::vector<double> lps = {-3.06772779477656,-0.977654036813297,-3.01475169747216,-0.578898745873913,-0.444846222063099,-6.65476914326284,-2.06142236468469,-3.9048004594152,-0.29627744117047,-2.79668565176668};
	assert(abs(logsumexp(lps)-0.965657562406778) < 0.000001);

}