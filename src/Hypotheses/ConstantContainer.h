
#pragma once 

#include <vector>

#include "Miscellaneous.h"
#include "Rule.h"


/**
 * @class ConstantContainer
 * @author Steven Piantadosi
 * @date 29/01/22
 * @file ConstantsHypothesis.h
 * @brief A ConstantContainer is a hypothesis that has a vector of real-valued constants. This is used in SymbolicRegression
 * 	      to store parameters etc. 
 * 
 * 		  Note that this not a hypothesis, but just a little struct to help keep track of things. It does implement
 *        many similar functions to a Bayesable though. 
 */
struct ConstantContainer {
public:
	std::vector<double> constants;
	size_t         constant_idx; // in evaluation, this variable stores what constant we are in 
	
	virtual size_t count_constants() const = 0; // must implement 
	virtual std::pair<double,double> constant_proposal(double) const = 0; // must implmenet
	virtual void randomize_constants() = 0; 
	
	virtual bool operator==(const ConstantContainer& h) const {
		return constants == h.constants;
	}
	
	virtual size_t hash() const {
		// hash includes constants so they are only ever equal if constants are equal
		size_t hsh = 1;
		for(size_t i=0;i<constants.size();i++) {
			hash_combine(hsh, i, constants[i]);
		}
		return hsh;
	}
	
//	virtual double constant_prior() const {
//		double lp = 0.0;
//		for(auto& c : constants) {
//			lp += normal_lpdf(c, 0.0, 1.0);
//		} 
//		return lp;
//	}


};