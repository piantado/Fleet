#pragma once 

#include "MCTSBase.h"

/**
 * @class FullMCTSNode
 * @author Steven Piantadosi
 * @date 22/12/20
 * @file MCTS.h
 * @brief This is a MCTS node that descends all the way until it builds a tree to a terminal -- usually it uses too much memory
 */
template<typename this_t, typename HYP>
class FullMCTSNode : public MCTSBase<this_t,HYP> {	
	using Super = MCTSBase<this_t,HYP>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	
	
    virtual generator<HYP&> search_one(HYP& current) override {
		
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+current.string()+"] ", this->nvisits);
				
		auto c = this->descend_to_evaluable(current); //sets current and returns the node. 
		c->process_evaluable(current);
		co_yield current;
    } 
	
};

