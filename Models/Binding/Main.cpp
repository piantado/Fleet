
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
std::vector<double> data_amounts = {0, 1, 2, 5, 10, 15, 20, 30, 40, 50, 75, 100, 110, 125, 150, 175, 200, 300, 400, 500}; // , 600, 700, 800, 900, 1000}; //, 1250, 1500, 2000, 2500, 5000, 7500, 10000};
//std::vector<double> data_amounts = {1000};

static const double alpha = 0.95; 
int NDATA = 10; // how many data points from each sentence are we looking at?
const double MAX_T = 100.0;

const double TERMINAL_P = 3.0;

std::string data_files = "data/data-full.csv"; // take a list of comma-separated file names

using S = std::string;

// should we include linear positions?
int include_linear = 0; 


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


MyHypothesis::data_t load_data(std::string filename) {
	MyHypothesis::data_t data;
	
	for(auto& [ds, sentence] : read_csv<2>(filename, false, ',')){
		
		BindingTree* t = new BindingTree(SExpression::parse(ds));	
		//print("t=", t->string());
		
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
		// print(t->string());
		
		for(int cr=0;cr<ncoref;cr++) {
			//raw_data.push_back(ds); // save for later in case we want to see
		
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
			
			//print("#", myt->string());
			
			MyHypothesis::datum_t d1 = {myt->get_target(), myt->get_target()->word, alpha};	
			
			// and store where we need them:
			data.push_back(d1);
		}
		
		delete t;
	}
	
	return data; 
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

MyHypothesis::data_t target_precisionrecall_data; // data for computing precision/recall 
MyHypothesis target;
	
int main(int argc, char** argv){ 
	
	Fleet fleet("Learn principles of binding theory");
	fleet.add_option("--ndata",   NDATA, "Run at a different likelihood temperature (strength of data)"); 
	fleet.add_option("--data",  data_files, "A comma-separated list of data files to include");
	fleet.add_flag("--include-linear",  include_linear, "Should we include primitives for linear order?"); 
	fleet.initialize(argc, argv);
	
	//------------------
	// Add linear hypotheses if needed
	//------------------
	
	if(include_linear) {		// linear order predicate
		grammar.add("gt_linear",  +[]() -> TxTtoBool { return  gt_linear; } );		
	}

	//------------------
	// set up the data
	//------------------
	

	// always compute precision/recall on the whole dataset
	target_precisionrecall_data = load_data("data/data-full.csv");
	
	// we have to loop over the comma-separated list of data files
	MyHypothesis::data_t mydata;
	for(auto& filename : split(data_files, ',')) {	
		print("# Loading data file", filename);
		auto d = load_data(filename);
		mydata.insert(std::end(mydata), std::begin(d), std::end(d));	
	}
	
	//------------------
	// Make target hypothesis to compare
	//------------------
	
	//"REXP", "him", "his", "he", "himself"
//	target["himself"] = InnerHypothesis(grammar.simple_parse("and(eq_pos('NP-O',pos(x)),applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x)))))))"));
//	target["him"]     = InnerHypothesis(grammar.simple_parse("and(eq_pos('NP-O',pos(x)),not(applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x))))))))"));
//	target["his"]     = InnerHypothesis(grammar.simple_parse("eq_pos('NP-POSS',pos(applyR(parent,x)))"));
//	target["he"]      = InnerHypothesis(grammar.simple_parse("eq_pos('NP-S',pos(x))"));
//	target["REXP"]    = InnerHypothesis(grammar.simple_parse("empty(filter(applyC(corefers,x),map_append(children,applyS(ancestors,applyR(parent,x)))))"));

	target["himself"] = InnerHypothesis(grammar.simple_parse("and(applyB(eq_pos('NP-O'),x),applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x)))))))"));
	target["him"]     = InnerHypothesis(grammar.simple_parse("and(applyB(eq_pos('NP-O'),x),not(applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x))))))))"));
	target["his"]     = InnerHypothesis(grammar.simple_parse("applyB(eq_pos('NP-POSS'),applyR(parent,x))"));
	target["he"]      = InnerHypothesis(grammar.simple_parse("applyB(eq_pos('NP-S'),x)"));
	target["REXP"]    = InnerHypothesis(grammar.simple_parse("empty(filter(applyC(corefers,x),map_append(children,applyS(ancestors,applyR(parent,x)))))"));

//
//	MyHypothesis t2; 
//	t2["himself"] = InnerHypothesis(grammar.simple_parse("and(applyB(eq_pos('NP-O'),x),applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x)))))))"));
//	t2["him"]     = InnerHypothesis(grammar.simple_parse("and(applyB(eq_pos('NP-O'),x),not(applyCC(corefers,x,head(filter(has_index,map_append(children,applyS(ancestors,applyR(parent,x))))))))"));
//	t2["his"]     = InnerHypothesis(grammar.simple_parse("applyB(eq_pos('NP-POSS'),applyR(parent,x))"));
//	t2["he"]      = InnerHypothesis(grammar.simple_parse("applyB(eq_pos('NP-S'),x)"));
//	t2["REXP"]    = InnerHypothesis(grammar.simple_parse("empty(filter(applyC(corefers,x),map_append(children,tail(append(filter(eq_posT(x),applyS(ancestors,x)),applyS(ancestors,x))))))"));
//
//
//	for(const auto& di : mydata) {
//		
//		for(const auto& w : words) {
//			target.clear_cache();	
//			t2.clear_cache();	
//			if(target[w].call(di.input) != t2[w].call(di.input)) {
//				target.clear_cache();	
//				t2.clear_cache();	
//				print(target[w].call(di.input),
//					  t2[w].call(di.input),
//					  di.input->root()->string());
//			}
//			
//		}
//	}
//
//return 1; 


	// check that the target works on the data
	for(const auto& di : mydata){ 
		
		// make a little mini dataset
		MyHypothesis::data_t thisdata;
		thisdata.push_back(di);
		
		target.clear_cache();	

		if(not target[di.output].call(di.input)) {
			print("## Error target does not predict data:", di.input->root()->string(), di.output);
		}

		target.compute_likelihood(thisdata);
		//print(target.likelihood, di.output, di.input->root()->string());
		assert(not std::isnan(target.likelihood));
		assert(not std::isinf(target.likelihood));
	}

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
		
		target.clear_cache();
		target.compute_posterior(mydata);
		print("# Target[", str(di), "]:", target.posterior, target.likelihood);
		
		ParallelTempering chain(h0, &mydata, FleetArgs::nchains, MAX_T);
		// MCMCChain chain(h0, &mydata);
		for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
			UNUSED(h);
		}

		// start from the best next time
		h0 = top.best();
		
		top.print();		
	}
	
	//------------------
	// Print all the data for the best hypothesis and the target
	//------------------
	
	auto best = top.best();
	
	for(size_t i=0;i<mydata.size();i++) {
		auto& di = mydata[i];
		
		std::vector<S> hwords;
		for(auto& w : words) {
			try { 			
				best.factors[w].clear_cache();	
				if(best.factors[w].call(di.input, false)) 
					hwords.push_back(w);
			} catch( TreeException& e) { }
		}

		
		std::vector<S> twords;
		for(auto& w : words) {
			try { 		
				target[w].clear_cache();		
				if(target[w].call(di.input, false))	
					twords.push_back(w);
			} catch( TreeException& e) { }
		}

		if(hwords != twords) {
			print("#", i, mydata.size(), di.input->root()->string()); //.at(i));
			print("#\tBest:",   str(hwords));
			print("#\tTarget:", str(twords));
		}
//		COUT "\t" << di.output << ":\t";
		

//		COUT "\n";
	}
}
