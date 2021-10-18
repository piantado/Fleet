#pragma once

#include "Bayesable.h"

/**
 * @class MCMCable
 * @author steven piantadosi
 * @date 03/02/20
 * @file MCMCable.h
 * @brief A class is MCMCable if it is Bayesable and lets us propose, restart, and check equality (which MCMC does for speed).
 */
template<typename this_t, typename... Args>
class MCMCable : public Bayesable<Args...> {
public:
	MCMCable() { }

	// these are declared nodiscard (and must be in subclasses too) to help remind that they do NOT modify the hypothesis,
	// instead they return a new one
	[[nodiscard]] virtual std::pair<this_t,double> propose() const = 0; // return a proposal and its forward-backward probs
	[[nodiscard]] virtual this_t                   restart() const = 0; // restart a new chain -- typically by sampling from the prior, but actually it only resamples can_resample 
	virtual bool operator==(const this_t& h)                 const = 0; // speeds up a check in MCMC
	
	/**
	 * @brief Static function for making a hypothesis. Be careful using this with references because they may not foward right (for reasons
	 * 			that are unclear to me)
	 * @return 
	 */
	[[nodiscard]] static this_t sample() {
		assert(false && "*** This should not be called -- must be overwritten in derived class");
	}
	
	
	virtual bool operator!=(const this_t& h)   const {
		return not this->operator==(h);
	}
};



