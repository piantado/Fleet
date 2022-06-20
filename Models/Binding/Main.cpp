
// Maybe add filter, higher-order predicates?

#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <exception>
#include <map>

#include "BindingTree.h"

// need a fixed order of words to correspond to factor levels
// We use REXP here (John, Mary, etc) so that we don't have to distinguish which
std::vector<std::string> words = {"REXP", "him", "his", "he", "himself"};
std::vector<double> data_amounts = {1, 2, 5, 10, 15, 20, 30, 40, 50, 75, 100, 110, 125, 150, 175, 200, 300, 400, 500, 1000, 1500, 2000, 2500, 5000};
//std::vector<double> data_amounts = {1300};

static const double alpha = 0.95; 
int NDATA = 10; // how many data points from each sentence are we looking at?
const double MAX_T = 100.0;

const double TERMINAL_P = 3.0;

using S = std::string;
int include_linear = 1; 
int include_absolute = 1; 

/**
 * @class TreeException
 * @author Steven Piantadosi
 * @date 08/09/21
 * @file Main.cpp
 * @brief TreeExceptions get thrown when we try to do something crazy in the tree
 */
class TreeException : public std::exception {};


#include "MyGrammar.h"
#include "MyHypothesis.h"

#include "Fleet.h" 

#include "TopN.h"
#include "Fleet.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"
#include "SExpression.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

MyHypothesis::data_t target_precisionrecall_data; // data for computing precision/recall 
MyHypothesis target;
	
int main(int argc, char** argv){ 
	
	Fleet fleet("Learn principles of binding theory");
	fleet.add_option("--ndata",   NDATA, "Run at a different likelihood temperature (strength of data)"); 
	fleet.add_option("--include-linear",  include_linear, "Should we include primitives for linear order?"); 
	fleet.add_option("--include-absolute",  include_absolute, "Should we include absolute primitives?"); 
	fleet.initialize(argc, argv);
	
	//------------------
	// Add linear hypotheses if needed
	//------------------
	
	if(include_absolute) {
		
		// for absolute positions (up top 24)
		for(int p=0;p<24;p++) {
			grammar.add_terminal<int>("abs"+str(p), p, TERMINAL_P/24.);
		}	
	}
	
	if(include_linear) {
		// linear order predicates
		grammar.add("linear(%s)",        +[](BindingTree* x) -> int { 
			if(x==nullptr) throw TreeException();			
			return x->linear_order; 
		});
		
		grammar.add("gt(%s,%s)",         +[](int a, int b) -> bool { return (a>b); });
		
	}

	// these primitives get included for either (but only once)
	if(include_absolute or include_linear) {
		
		grammar.add("eq_int(%s,%s)",   +[](int a, int b) -> bool { return (a==b); });		
		
		grammar.add("if(%s,%s,%s)",  Builtins::If<MyGrammar,int>);
	}
	


	//------------------
	// set up the data
	//------------------
	
	// convert to data
	MyHypothesis::data_t mydata;
	std::vector<std::string> raw_data; 	
	for(auto& [ds, sentence] : read_csv<2>("lasnik-extensive.csv", false, ',')){
		
		// look at tokenization
		// for(auto k : SExpression::tokenize(ds)) { COUT ">>" << k << "<<" ENDL;}
		
		BindingTree* t = new BindingTree(SExpression::parse<BindingTree>(ds));	
		t->convert_from_SExpression(); // fix the fact that as S-expressions, the words are listed as first children
		PRINTN("t=", t->string());
		
		assert(t != nullptr);
		
		// and set the linear order of t
		// and count how many have coreferents (we need to make that many copies)
		int lc=0;
		int ncoref = 0;
		for(auto& n : *t) { 
			
			if(n.referent != -1 and n.nchildren()==0) // we only count simple terminal ones
				ncoref++;
			
			if(n.is_terminal()) n.linear_order = lc++;
			else                n.linear_order = -1;
		}
		// PRINTN(t->string());
		
		for(int cr=0;cr<ncoref;cr++) {
			raw_data.push_back(ds); // save for later in case we want to see
		
			BindingTree* myt = new BindingTree(*t);

			// find the i'th coreferent and label it as such
			int cntcoref = 0;
			for(auto& n : *myt) {
				if(n.referent != -1 and n.nchildren()==0) 
					cntcoref++;
					
				if(cntcoref > cr) {
					n.target = true;
					break;
				}
			}
			
			// assert that there is only one target per tree
			assert( myt->sum( +[](const BindingTree& bt) -> double { return 1.0*bt.target; }) == 1); 
			
			//PRINTN("#", myt->string());
			
			MyHypothesis::datum_t d1 = {myt->get_target(), myt->get_target()->word, alpha};	
			mydata.push_back(d1);
			
			target_precisionrecall_data.push_back(d1);
		}
		
		delete t;
	}
		
//	return 0;
	
	
	//------------------
	// Make target hypothesis to compare
	//------------------
	
	//"REXP", "him", "his", "he", "himself"
	target["REXP"]    = InnerHypothesis(grammar.simple_parse("not(and(corefers(x),dominates(parent(coreferent(x)),x)))"));
	target["him"]     = InnerHypothesis(grammar.simple_parse("eq_bool(eq_pos('NP-O',pos(x)),null(first-dominating('PP',x)))"));
	target["his"]     = InnerHypothesis(grammar.simple_parse("eq_pos('NP-POSS',pos(parent(x)))"));
	target["he"]      = InnerHypothesis(grammar.simple_parse("eq_pos('S',pos(parent(x)))"));
	target["himself"] = InnerHypothesis(grammar.simple_parse("and(corefers(x),dominates(parent(coreferent(x)),x))"));

//	MyHypothesis target2;
//	target2["REXP"] = InnerHypothesis(grammar.simple_parse("not(and(corefers(x),dominates(parent(coreferent(x)),x)))"));
//	target2["him"] = InnerHypothesis(grammar.simple_parse("eq_bool(eq_pos('NP-O',pos(x)),null(first-dominating('PP',x)))"));
//	target2["his"] = InnerHypothesis(grammar.simple_parse("eq_pos('NP-POSS',pos(parent(x)))"));
//	target2["he"] = InnerHypothesis(grammar.simple_parse("eq_pos('S',pos(parent(x)))"));
//	target2["himself"] = InnerHypothesis(grammar.simple_parse("and(eq_bool(eq_pos('NP-POSS',pos(parent(x))),eq_pos('NP-S',pos(x))),corefers(x))"));
//	
//	// Find sentences where these are different
//	for(auto& di : mydata){ 
//	
//		// make a little mini dataset
//		MyHypothesis::data_t thisdata;
//		thisdata.push_back(di);
//		
//		for(auto& w:words) {
//			target[w].clear_cache();
//			target2[w].clear_cache();
//		}
//		
//		target.compute_posterior(thisdata);
//		target2.compute_posterior(thisdata);
//		
//		if(target.likelihood != target2.likelihood)  {
//			PRINTN(target.likelihood, target2.likelihood, di.input->root()->string());
//		}
//	
//	}
//	
//	return 0;
//	
	
	
	PRINTN("# Target:", target.compute_posterior(mydata));


	//////////////////////////////////
	// run on increasing amounts of data
	//////////////////////////////////

	TopN<MyHypothesis> top;

	// set up the hypothesis
	MyHypothesis h0;
	for(const auto& w : words) {
		h0[w] = InnerHypothesis::sample();
	}
	MyHypothesis::p_factor_propose = 0.2;
	
	
	for(auto di : data_amounts) {
		
		// update data and top
		NDATA = di; // used in compute_posterior
		top = top.compute_posterior(mydata);
		
		ParallelTempering chain(h0, &mydata, FleetArgs::nchains, MAX_T);
		for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
			UNUSED(h);
		}

		// start from the best next time
		h0 = top.best();
		
		top.print();		
	}
	
	//////////////////////////////////
	// run MCMC
	//////////////////////////////////
	
//	TopN<MyHypothesis> top;
////	top.print_best = true;
//
//	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 1.20);
////	MCMCChain chain(h0, &mydata);
//	for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
//		UNUSED(h);
//	}
//	
//	top.print();

	//------------------
	// Print all the data
	//------------------
		
//	for(auto& f : best.factors) f.program.loader = &best; 
//	
//	for(size_t i=0;i<mydata.size();i++) {
//		auto& di = mydata[i];
//		PRINTN("#", i, mydata.size(), di.input->root()->string()); //.at(i));
//		
//		size_t fi = 0;
//		for(auto& w : words) {
//	
//			int b = -1; // default value
//			try { 				
//				b = 1*best.factors[fi].callOne(di.input, false); 
//			} catch( TreeException& e) {  }
//			PRINTN("#", b, w);
//			++fi;
//		}
//	}
}