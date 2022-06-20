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
//const std::map<S,size_t> symbol2idx = {{"true",0}, {"false",1}, {"and",2}, {"or",3}, {"not",4}};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Combinators.h"
#include "DeterministicLOTHypothesis.h"

using CL=Combinators::CL;

#include "CLNode.h"

/**
 * @class CLDatum
 * @author Steven Piantadosi
 * @date 28/05/22
 * @file Main.cpp
 * @brief A datatype for a CL datum involving a LHS and a RHS consisting of BaseNodes
 * 		  with the right labels
 */
//struct CLDatum { 
//	CLNode lhs;
//	CLNode rhs;
//	bool equal = true; 
//	
//	CLDatum(S& l, S& r, bool e=true) : lhs(l), rhs(r), equal(e) {
//	}
//};

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
class MyHypothesis final : public Lexicon<MyHypothesis, S, InnerHypothesis, CL, CL> {
public:	
	
	using Super = Lexicon<MyHypothesis, S, InnerHypothesis, CL, CL>;
	using Super::Super;	

//	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// Here we have just written by hand all of the relations we want
		// This differs from churiso in that churiso will push constraints when possible
		
		// Enforce uniqueness
//		for(auto& x : symbols) 
//		for(auto& y : symbols) {
//			if(x != y and C(x) == C(y)) {
//				return likelihood = -infinity;
//			}
//		}
//	
//		// And check these bounds
//		int error = 0;	
//		error += (apply(C("not"), C("true"))  != C("false"));
//		error += (apply(C("not"), C("false")) != C("true"));
//		
//		error += (apply(apply(C("and"), C("false")), C("false")) != C("false"));
//		error += (apply(apply(C("and"), C("false")), C("true"))  != C("false"));
//		error += (apply(apply(C("and"), C("true")), C("false"))  != C("false"));
//		error += (apply(apply(C("and"), C("true")), C("true"))   != C("true"));
//		
//		error += (apply(apply(C("or"), C("false")), C("false")) != C("false"));
//		error += (apply(apply(C("or"), C("false")), C("true"))  != C("true"));
//		error += (apply(apply(C("or"), C("true")), C("false"))  != C("true"));
//		error += (apply(apply(C("or"), C("true")), C("true"))   != C("true"));
//				
//		likelihood = -100.0*error; 
//		return likelihood; 
//	
//	 }
	 
};



////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "TopN.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"
#include "SExpression.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	Fleet fleet("Combinatory logic");
	fleet.initialize(argc, argv);
	
	for(size_t k=0;k<1000;k++) {
		try {
			auto x = Combinators::skgrammar.generate();
		
			CLNode n{x};
			print(n.string());
			n.reduce();
			print(n.string());
			print("------------");
		} catch(Combinators::ReductionException& e) {
			
		}
	}
	
	auto g = SExpression::parse<CLNode>("((K K) S)");
	
	print(g.string());
	
	g.reduce();
	
	print(g.string());
	
	return 0;
	
//	Combinators::LazyNormalForms lnf;
//	for(size_t i=0;!CTRL_C;i++) {
//		Node n = lnf.at(i);
//		auto s = n.string();
//		auto len = n.count_terminals();
//		Fleet::Combinators::reduce(n);
//		COUT i TAB len TAB s TAB n.string() ENDL;
//	}

	MyHypothesis h0 = MyHypothesis::sample(symbols);
//	for(auto x : symbols) {;
//		h0.factors.push_back(InnerHypothesis::sample());
//	}
	
	TopN<MyHypothesis> top;
	
	MyHypothesis::data_t dummy_data;
	
	ParallelTempering chain(h0, &dummy_data, FleetArgs::nchains, 10.0);
	for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
		UNUSED(h);
	}
	
	top.print();
}