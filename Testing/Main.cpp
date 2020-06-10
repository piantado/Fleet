#include <string>
#include <map>

#define DO_NOT_INCLUDE_MAIN 1 
#include "../Models/FormalLanguageTheory-Simple/Main.cpp"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename Grammar_t>
void checkNode(const Grammar_t* g, const Node& n) {
	
	// first check the iterator hits all the nodes:
	// NOTE: this works because count does not use the iterator, it uses recursion
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
	
	// check that if we convert to names and back, we get an equal node
	Node q = g->expand_from_names(n.parseable());
	assert(q == n);
	assert(&q != &n);
	
	// check the log probability that I get out. 
	// by counting how things are mapped to strings
	// NOTE: This is specific to this grammar
//	std::string s = n.string();
//	double mylp = 0.0;
//	double lz = log(zprob()); // TODO: This should only be computed once...
//	for(auto& i : probs) { 
//		mylp += count(s, i.first) * (log(i.second) - lz); // TODO: 
//	}
	//CERR mylp TAB g->log_probability(n) TAB n ENDL;
//	assert(abs(mylp-g->log_probability(n)) < 0.00001);
}


template<typename Grammar_t, typename Hypothesis_t>
void checkLOTHypothesis(const Grammar_t* g, const Hypothesis_t h){
	checkNode(g, h.get_value());
	
	// we should only be calling this after we've computed a posterior, so these should be set
	assert(not std::isnan(h.prior));		
	assert(not std::isnan(h.likelihood));		
	assert(not std::isnan(h.posterior));		

	// Check that copies and things work right:
	decltype(h) newH = h;
	checkNode(g, newH.get_value());
	assert(newH == h);
	assert(newH.get_value() == h.get_value());
	assert(newH.posterior == h.posterior);
	assert(newH.prior == h.prior);
	assert(newH.likelihood == h.likelihood); // NOTE: We somtimes might fail on these, but that's usually from NaNs, which don't evaluate to equal to each other. 
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
//		assert(h.posterior > -infinity); // not necessarily required anymore
		assert(h.posterior < infinity);
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
#include "EnumerationInference.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Testing");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)	
	MyGrammar grammar(PRIMITIVES);
	
	// here we create an alphabet op with an "arg" that stores the character (this is faster than alphabet.substring with i.arg as an index) 
	// here, op_ALPHABET converts arg to a string (and pushes it)
	for(size_t i=0;i<alphabet.length();i++) {
		grammar.add<S>     (BuiltinOp::op_ALPHABET, Q(alphabet.substr(i,1)), 5.0/alphabet.length(), (int)alphabet.at(i)); 
	}
	
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	// initial hypothesis
	MyHypothesis h0(&grammar);
	
	//------------------
	// set up the data
	//------------------

	// we will parse the data from a comma-separated list of "data" on the command line
	for(auto di : split(datastr, ',')) {
		// add check that data is in the alphabet
		for(auto& c : di) {
			assert(alphabet.find(c) != std::string::npos && "*** alphabet does not include all data characters");
		}		
		// and add to data:
		mydata.push_back( MyHypothesis::datum_t({S(""), di}) );		
	}
	
	const size_t N = 100000; // check equality on the top this many
	
	//------------------
	// Actually run
	//------------------
	
	COUT "# MCMC...";
	TopN<MyHypothesis> top_mcmc(N);  //	top_mcmc.print_best = true;
	h0 = h0.restart();
	MCMCChain chain(h0, &mydata, top_mcmc);
	chain.run(Control(mcmc_steps,runtime));
	checkTop(&grammar, top_mcmc);
	assert(not top_mcmc.empty());
	COUT "GOOD" ENDL;
	
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
	
	
	COUT "# Parallel Tempering...";
	TopN<MyHypothesis> top_tempering(N);
	h0 = h0.restart();
	ParallelTempering samp(h0, &mydata, top_tempering, 8, 1000.0, false);
	samp.run(Control(mcmc_steps, runtime, nthreads), 500, 1000);	// we run here with fast swaps, adaptation to fit more in 
 	// NOTE: Running ParallelTempering with allcallback (default) will try to put
	// *everything* into top, which means that the counts you get will no longer 
	// be samples (and in fact should be biased towards high-prior hypotheses)	
	checkTop(&grammar, top_tempering);
	assert(not top_tempering.empty());
	COUT "GOOD" ENDL;
	
	COUT "# top_difference(top_mcmc, top_tempering) = " << top_difference(top_mcmc, top_tempering) ENDL;

	COUT "# Enumeration...";
	TopN<MyHypothesis> top_enumerate(N);
	EnumerationInference<MyHypothesis,MyGrammar,decltype(top_enumerate)> e(&grammar, grammar.nt<S>(), &mydata, top_enumerate);
	e.run(Control(mcts_steps, runtime, nthreads));
	assert(not top_enumerate.empty());
	checkTop(&grammar, top_enumerate);
	COUT "GOOD" ENDL;
	
	COUT "# Prior sampling...";
	TopN<MyHypothesis> top_generate(N);
	for(enumerationidx_t z=0;z<1000 and !CTRL_C;z++) {
		auto n = grammar.generate<S>();
		checkNode(&grammar, n);
		
		MyHypothesis h(&grammar);
		h.set_value(n);
		h.compute_posterior(mydata);
		
		top_generate << h;
	}
	checkTop(&grammar, top_generate);
	assert(not top_generate.empty());
	COUT "GOOD" ENDL;
	
	
	COUT "# top_difference(top_mcmc, top_generate) = " << top_difference(top_mcmc, top_generate) ENDL;

	// Let's check that when we enumerate, we don't get repeats
	COUT "# Checking enumeration is unique...";
	const size_t N_checkdup = 1000000;
	std::set<Node> s;
	for(enumerationidx_t z=0;z<N_checkdup and !CTRL_C;z++) {
		auto n = expand_from_integer(&grammar, grammar.nt<S>(), z);
		checkNode(&grammar, n);
		
		// this should give me back the right enumeration order
		assert(z == compute_enumeration_order(&grammar,n));
		
		// we should not have duplicates
		if(not s.empty()) {
			auto x = *s.begin();
			assert(not s.contains(n));
		}
		s.insert(n);
	}
	COUT "GOOD" ENDL;
	

	// And let's do one final check of enumeration using a grammar where we know the number of strings
	// that can be generated -- a Dyck grammar. Note that in order for this to count correctly, the grammar
	// cannot allow mutliple derivations of the same string -- if it did, it would count each of these derivations
	// separately. For instance, we can't have S->SS, S->[], S->[S] because that grammar allows multiple
	// derivations of [][][] (either right first or left first). So we use this format for Dyck. 
	COUT "# Checking counts on enumeration...";
	std::vector<int> dyckShouldBe = {1, 1, 2, 5, 14, 42}; // we actually don't get very far with this
	size_t checkDyckTill = dyckShouldBe.size();
	std::vector<int> count(checkDyckTill,0);
	std::tuple DYCK_PRIMITIVES = {
		Primitive("(%s)%s",  +[](S x, S y) -> S { throw YouShouldNotBeHereError();	}),
		Primitive("",        +[]()         -> S { throw YouShouldNotBeHereError();	}),
	};
	MyGrammar dyck_grammar(DYCK_PRIMITIVES);
	for(enumerationidx_t z=0;z<500000 and !CTRL_C;z++) {
		auto n = expand_from_integer(&dyck_grammar, dyck_grammar.nt<S>(), z);
		checkNode(&dyck_grammar, n);
		
		auto l = n.string().length()/2;
		if(l < checkDyckTill)
			count[l]++; // count the number of *pairs*
	}
	assert(std::equal(dyckShouldBe.begin(), dyckShouldBe.end(), count.begin()));
	COUT "GOOD" ENDL;
	
	
	
	/*	
	 * What we want to check:
	 * 	- each tree matches its probability
	 *  - check multicore performance 
	*/
	
	COUT "# Checking logsumexp..."; // paste(as.character(-exp(rnorm(10))), collapse=",")
	std::vector<double> lps = {-3.06772779477656,-0.977654036813297,-3.01475169747216,-0.578898745873913,-0.444846222063099,-6.65476914326284,-2.06142236468469,-3.9048004594152,-0.29627744117047,-2.79668565176668};
	assert(abs(logsumexp(lps)-0.965657562406778) < 0.000001);
	COUT "GOOD" ENDL;

}