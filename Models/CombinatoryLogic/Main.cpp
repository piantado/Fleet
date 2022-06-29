// Add catch for binary trees -- if they're not, then the evaluator gets messed up

#include <assert.h>

/* Re-implement churiso here, to do this, we can use integer coding to enumerate trees in a smart way
 */
#include <tuple>
#include <map>
#include <string>
#include <vector>

using S = std::string; 

// this maps each symbol to an index; 
std::vector<S> symbols;// = {"true", "false", "and", "or", "not"};
//const std::vector free_symbols = {"true", "not", "and", "or"}; // if we search, which ones are "free" variables? (e.g. from which all others are defined on the rhs?)

const double LL_PENALTY = 100;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Combinators.h"
#include "DeterministicLOTHypothesis.h"

using CL = Combinators::CL;
using SExpNode = SExpression::SExpNode;

/**
 * @class CLDatum
 * @author Steven Piantadosi
 * @date 28/05/22
 * @file Main.cpp
 * @brief A datatype for a CL datum involving a LHS and a RHS consisting of BaseNodes
 * 		  with the right labels
 */
struct CLDatum { 
	SExpNode lhs;
	SExpNode rhs;
	bool equal = true; 
	
	CLDatum(std::string s) {
		
		auto [l, r] = split<2>(s, '=');
		
		lhs = SExpression::parse(l);
		rhs = SExpression::parse(r);

		::print("# LHS=", QQ(l), SExpression::parse(l).string(), lhs.string());
		::print("# RHS=", QQ(r), SExpression::parse(r).string(), rhs.string());
	}
};


#include "MyHypothesis.h"

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "TopN.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"
#include "Enumeration.h"
#include "Strings.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	FleetArgs::input_path = "domains/boolean.txt"; // default
	
	Fleet fleet("Combinatory logic");
	fleet.initialize(argc, argv);
	
	MyHypothesis::data_t mydata;
	for(auto& line : split(gulp(FleetArgs::input_path), '\n')) {
		if(is_prefix<std::string>("symbols ", line)) { // process symbol lists
			auto s = split(line, ' '); 
			s.pop_front(); // skip the first
			for(auto& x : s) 
				symbols.push_back(x);
		}
		else if(line.length() > 2) { // skip whitespace
			mydata.push_back(CLDatum{line});
		}
	}

	MyHypothesis h0 = MyHypothesis::sample(symbols);
	TopN<MyHypothesis> top;
	top.print_best = true;
	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 1000.0);
	for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
		UNUSED(h);
	}
	
	top.print();


	// TODO: Here we really should implement a churiso-like inference maybe relying on enumeration

//	TopN<MyHypothesis> top;
//	top.print_best = true;
//	for(enumerationidx_t e=0;!CTRL_C;e++) {
//		IntegerizedStack is{e};
//		
//		MyHypothesis h; 
//		
//		// we split here into the defining terms
//		auto values = is.split(free_symbols.size());
//		for(size_t i=0;i<values.size();i++) {
//			//::print(e, i, free_symbols[i], values[i]);
//			IntegerizedStack q{values[i]};
//			h[free_symbols[i]] = expand_from_integer(&Combinators::skgrammar, Combinators::skgrammar.nt<CL>(), q);
//		}
//		
//		
//		//::print(h);
//
//		try { 
//
//			// now go through and push constraints
//			// NOTE: Here we need a way to convert a CL node back into a normal node!
//			// NOTE: here we only push left->right
//			for(const auto& d : mydata) {
//				CLNode rhs = d.rhs; 
//				CLNode lhs = d.lhs; // make a copy
//				lhs.substitute(h); // fill in what we got
//				if(lhs.is_only_CL() and rhs.nchildren() == 0 and not h.contains(rhs.label)) {
//					lhs.reduce();
//					InnerHypothesis ihr; ihr.set_value(lhs.toNode(), false); // convert to an Node, then an InnerHypothesis and push as the value
//					h[rhs.label] = ihr; // push this constraint
//				}
//			}
//	//		
//
//			h.compute_posterior(mydata);
//			
//			top << h;
//		} catch(Combinators::ReductionException& err) { } // just skip on reduciton errors
//	}
//	
//	top.print();

//return 0;

}



/* Some old testing code:



//	size_t cnt = 256; 
//	SExpression::SENode k = SExpression::parse("(((S ((S K) S)) (K K) ) (K K))");
//	print(k.string());	
//	CLreduce(k,cnt);
//	print(k.string());	
//	return 0;

	// for testing reduction in the boolean case
	
//	CLNode k = SExpression::parse("((S ((S K) S)) (K K) )");
//	print(k.string());
//	k.reduce();
//	print(k.string());	
//	return 0;
	
//	auto ih = InnerHypothesis("((S ((S K) S)) (K K) )");
//	
//	print(ih.string());
//	CLNode k = ih.get_value();
//	print(k.string());
//	k.reduce();
//	print(k.string());
//	
//	return 0;
	
//	MyHypothesis tst;
//	tst["and"] = InnerHypothesis("((S (S (S S))) (K (K K)))");
//	tst["or"]  = InnerHypothesis("((S S) (K (K K)))");
//	tst["not"] = InnerHypothesis("((S ((S K) S)) (K K) )");
//	tst["true"]  = InnerHypothesis("(K K)");
//	tst["false"]  = InnerHypothesis("(K)");
//
//	auto d1 = mydata[0].lhs;
//	print(d1.string());
//	substitute(d1, tst);
//	print(d1.string());
//	
//	tst.compute_posterior(mydata);
//	print(tst.string());
//	print(tst.posterior);
//	
//	return 0;

*/