
#pragma once 

#include <vector>

#include "Miscellaneous.h"
#include "Rule.h"
#include "Constant.h"	

struct TooManyConstantsException : public VMSRuntimeError {};

	



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
	std::vector<Constant> constants;
	size_t                constant_idx; // in evaluation, this variable stores what constant we are in 
	
	virtual size_t count_constants() const = 0; // must implement 
	virtual std::pair<double,double> constant_proposal(Constant) const = 0; // must implement
	virtual void randomize_constants() = 0; 
	
	// used by some classes to convert constants to parseable and back
	const static char ParseableDelimiter = '@'; // delimit format
	
	ConstantContainer() : constants{}, constant_idx(0) {
		
	}
	
	virtual void reset_constant_index() {
		constant_idx = 0;
	}
	
	virtual Constant next_constant() {
		
		// now we might have too many since the size of our constants is constrained
		if(constant_idx >= constants.size()) { 
			throw TooManyConstantsException();
		}
		else { 
			return constants.at(constant_idx++);
		}
	}
	
	virtual bool operator==(const ConstantContainer& h) const {
		auto C = count_constants();
		
		if(h.count_constants() != C) 
			return false; 
		
		for(size_t i=0;i<C;i++) {
			if(constants.at(i).get_value() != h.constants.at(i).get_value()) {
				return false; 
			}
		}
		return true; 
	}
	
	virtual size_t hash() const {
		// hash includes constants so they are only ever equal if constants are equal
		auto C = count_constants();
		size_t hsh = 1;
		for(size_t i=0;i<C;i++) {
			hash_combine(hsh, i, constants.at(i).get_value());
		}
		return hsh;
	}
	
};

