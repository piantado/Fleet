#pragma once

#include "Bayesable.h"

/**
 * @class MCMCable
 * @author steven piantadosi
 * @date 03/02/20
 * @file MCMCable.h
 * @brief A class is MCMCable if it is Bayesable and lets us propose, restart, and check equality (which MCMC does for speed).
 */
template<typename HYP, typename ...Args>
class MCMCable : public Bayesable<Args...> {
public:
	MCMCable() { }

	// these are declared nodiscard (and must be in subclasses too) to help remind that they do NOT modify the hypothesis,
	// instead they return a new one
	[[nodiscard]] virtual std::pair<HYP,double> propose() const = 0; // return a proposal and its forward-backward probs
	[[nodiscard]] virtual HYP                   restart() const = 0; // restart a new chain -- typically by sampling from the prior 
	virtual bool operator==(const HYP& h)   const = 0; // speeds up a check in MCMC
	
};



