#pragma once 

#include "DeterministicLOTHypothesis.h"

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

	virtual double compute_prior() override {
		
		// enforce uniqueness among the symbols 
		std::vector<CLNode> seen; 		
		for(auto& x : free_symbols) {
			CLNode xn(at(x).get_value());
			
			try{
				xn.reduce();
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
			CLNode lhs = d.lhs; // make a copy
			CLNode rhs = d.rhs; 
			
			try { 
				//::print(lhs.string(), rhs.string());
				
				lhs.substitute(*this);
				rhs.substitute(*this);
				//::print("\t", lhs.string(), rhs.string());
				lhs.reduce();
				rhs.reduce();
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
