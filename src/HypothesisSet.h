
#pragma once
#include<set>

template<typename HYP>
class HypothesisSet {
/**
 * @class HypothesisSet
 * @author piantado
 * @date 23/04/20
 * @file HypothesisSet.h
 * @brief A HypothesisSet stores a collection of hypotheses with the interface of std::set so that you can 
 *        add, union, etc. It also allows convenient manipulations like recomputing posterior on some data
 */
 
	std::set<HYP> s;
	
	HypothesisSet() {
		
		
	}
	
	
	void print() const {
		
	}
	
	[[nodiscard]] HypothesisSet<HYP> compute_posterior(const HYP::t_data& d) const { 
		
	}
	
	
};