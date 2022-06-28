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
const std::vector<S> symbols = {"true", "false", "and", "or", "not"};
//const std::vector<S> symbols = {"first", "rest", "cons"};
//const std::vector<S> symbols = {"succ", "one", "two", "three", "four"};

const double LL_PENALTY = 100;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Combinators.h"
#include "DeterministicLOTHypothesis.h"

using CL=Combinators::CL;

#include "CLNode.h"
#include "SExpression.h"

/**
 * @class CLDatum
 * @author Steven Piantadosi
 * @date 28/05/22
 * @file Main.cpp
 * @brief A datatype for a CL datum involving a LHS and a RHS consisting of BaseNodes
 * 		  with the right labels
 */
struct CLDatum { 
	CLNode lhs;
	CLNode rhs;
	bool equal = true; 
	
	CLDatum(std::string s) {
		
		auto [l, r] = split<2>(s, '=');
		
		lhs = SExpression::parse(l);
		rhs = SExpression::parse(r);

		::print("#LHS=", QQ(l), SExpression::parse(l).string(), lhs.string());
		::print("#RHS=", QQ(r), SExpression::parse(r).string(), rhs.string());
	}
};

/**
 * @class InnerHypothesis
 * @author piantado
 * @date 04/05/20
 * @file Main.cpp
 * @brief This just stores nodes from SKGrammar, and doesn't permit calling input/output
 */
class InnerHypothesis final : public DeterministicLOTHypothesis<InnerHypothesis,CL,CL,Combinators::SKGrammar,&Combinators::skgrammar> {
public:
	using Super = DeterministicLOTHypothesis<InnerHypothesis,CL,CL,Combinators::SKGrammar,&Combinators::skgrammar>;
	using Super::Super; // inherit constructors
};


#include "Lexicon.h"

/**
 * @class MyHypothesis
 * @author piantado
 * @date 04/05/20
 * @file Main.cpp
 * @brief A version of Churiso that works with MCMC on a simple boolean example
 */
class MyHypothesis final : public Lexicon<MyHypothesis, S, InnerHypothesis, CL, CL, CLDatum> {
public:	
	
	using Super = Lexicon<MyHypothesis, S, InnerHypothesis, CL, CL, CLDatum>;
	using Super::Super;	

	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		
		
		// enforce uniqueness among the symbols 
		for(auto& x : symbols) 
		for(auto& y : symbols) {
			if(x != y) {
				
				// check to be sure their reduced forms are different
				CLNode xn = at(x).get_value(); // make a copy and convert to CLNode
				CLNode yn = at(y).get_value(); 
				
				try {
					xn.reduce();
					yn.reduce();
				} catch(Combinators::ReductionException& e) {
					return -infinity; 
				}
				
				if(xn == yn){
					return likelihood = -infinity;
				}
			}
		}
		//::print(string());
		
		likelihood = 0.0;
		for(auto& d : data) {
			CLNode lhs = d.lhs; // make a copy
			CLNode rhs = d.rhs; 
			
			try { 
				//::print(lhs.string(), rhs.string());
				
				lhs.substitute(*this);
				rhs.substitute(*this);
				//::print("\t", lhs.string(), rhs.string());
				lhs.reduce();
				rhs.reduce();
				//::print("\t", lhs.string(), "--------", rhs.string());
				//::print("\t", (lhs==rhs));
				// check if they are right 
				if((lhs == rhs) != (d.equal == true)) {
					likelihood -= LL_PENALTY;
				}
			} catch(Combinators::ReductionException& e) {
				likelihood -= LL_PENALTY; // penalty for exceptions 
			}
		}

		return likelihood; 
	
	 }
	 
};



////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "TopN.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"


#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	Fleet fleet("Combinatory logic");
	fleet.initialize(argc, argv);
	
//	for(size_t k=0;k<1000;k++) {
//		try {
//			CLNode x{Combinators::skgrammar.generate()};
//
//			// let's check that our S-expression parser is doing what we want
//			auto y = SExpression::parse<CLNode>(x.string());
//			print(x.fullstring());
//			print(y.fullstring());
//			assert(x==y);
//			
//			CLNode n{x};
//			print(n.string());
//			n.reduce();
//			print(n.string());
//			print("------------");
//		} catch(Combinators::ReductionException& e) {
//			print("Reduction error");
//		}
//	}
//
//	return 0;

	// NOTE: The data here MUST be binary trees
	std::vector<std::string> data_strings = {
		"((and false) false) = false", 
		"((and false) true) = false", 
		"((and true) false) = false", 
		"((and true) true) = true",
		
		"((or false) false) = false", 
		"((or false) true) = true", 
		"((or true) false) = true", 
		"((or true) true) = true",
		
		"(not false) = true", 
		"(not true) = false"
	};

//	std::vector<std::string> data_strings = {
//		"(first ((cons x) y)) = x", 
//		"(rest  ((cons x) y)) = y"
//	};
//	
//	std::vector<std::string> data_strings = {
//		"(succ one) = two", 
//		"(succ two) = three",
//		"(succ three) = four"
//	};
	

	MyHypothesis::data_t mydata;
	for(auto& ds : data_strings) {
		mydata.push_back(CLDatum{ds});
	}

//return 0;

	MyHypothesis h0 = MyHypothesis::sample(symbols);

	TopN<MyHypothesis> top;
	top.print_best = true;
	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 10.0);
	for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
		UNUSED(h);
	}
	
	top.print();
}