#pragma once

#include "Bayesable.h"

template<typename HYP, typename ...Args>
class MCMCable : public Bayesable<Args...> {
public:
	MCMCable() { }

	virtual std::pair<HYP,double> propose() const = 0; // return a proposal and its forward-backward probs
	virtual HYP                   restart() const = 0; // restart a new chain -- typically by sampling from the prior 
	//virtual bool operator==(const MCMCable<HYP,Args...>& h) const = 0; // speeds up a check in MCMC
	
};



