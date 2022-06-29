#pragma once 

#include "DeterministicLOTHypothesis.h"
#include "Combinators.h"

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
	
	InnerHypothesis(std::string s) {
		Node n = Combinators::SENodeToNode(SExpression::parse(s));
		this->set_value(std::move(n), false);
	}
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

	virtual double compute_prior() override {
		
		// enforce uniqueness among the symbols 
		std::vector<SExpression::SENode> seen; 		
		for(auto& x : symbols) {
			SExpression::SENode xn = Combinators::NodeToSENode(at(x).get_value());
			
			try{
				Combinators::reduce(xn);
			} catch(Combinators::ReductionException& e) {
				return prior = -infinity;
			}
			
			if(std::find(seen.begin(), seen.end(), xn) != seen.end()) {// see if we have it
				return prior = -infinity;
			}
			else {
				seen.push_back(xn);
			}
		}
		
		return Super::compute_prior();
	}
		
	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		
		//::print(string());
		
		likelihood = 0.0;
		for(const auto& d : data) {
			SExpression::SENode lhs = d.lhs; // make a copy
			SExpression::SENode rhs = d.rhs; 
			
			try { 
				//::print(lhs.string(), rhs.string());
				
				Combinators::substitute(lhs, *this);
				Combinators::substitute(rhs, *this);
				//::print("\t", lhs.string(), rhs.string());
				Combinators::reduce(lhs);
				Combinators::reduce(rhs);
				//::print("\t", lhs.string(), rhs.string());
				//::print("\t", (lhs==rhs));
				// check if they are right 
				if((lhs == rhs) != (d.equal == true)) {
					//::print("\t", d.lhs.string(), d.rhs.string());
					likelihood -= LL_PENALTY;
				}
			} catch(Combinators::ReductionException& e) {
				likelihood -= LL_PENALTY; // penalty for exceptions 
			}
		}
		
		//::print(likelihood);

		return likelihood; 
	
	 }
	 
};
