#pragma once

#include "Bayesable.h"

template<typename HYP, typename t_input, typename t_output, typename _t_datum=default_datum<t_input, t_output>>
class MCMCable : public Bayesable<t_input,t_output,_t_datum> {
public:
	MCMCable() { }

	virtual std::pair<HYP,double> propose() const = 0; // return a proposal and its forward-backward probs
	virtual HYP                   restart() const = 0; // restart a new chain -- typically by sampling from the prior 
	
};



