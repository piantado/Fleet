#pragma once

#include "Bayesable.h"

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



