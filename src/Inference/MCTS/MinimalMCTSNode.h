#pragma once 

#include "MCTSBase.h"

/**
 * @class MinimalMCTSNode
 * @author piantado
 * @date 03/07/20
 * @file MCTS.h
 * @brief When this gets to a leaf, it expands the highest probability things from the prior. But it uses the same sampling scheme to reach a leaf
 */
template<typename this_t, typename HYP>
class MinimalMCTSNode : public MCTSBase<this_t,HYP> {	
	using Super = MCTSBase<this_t,HYP>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	
	virtual generator<HYP&>  search_one(HYP& current) override {
		if(DEBUG_MCTS) DEBUG("MinimalMCTSNode SEARCH ONE ", this, "\t["+current.string()+"] ", this->nvisits);
	
		auto c = this->descend_to_childless(current); //sets current and returns the node. 
		
		while(not current.is_evaluable()) {
			c->add_children(current); 
			
			// find the highest prior child
			auto neigh = current.neighbors();
			std::vector<double> children_lps(neigh,-infinity);
			for(int k=0;k<neigh;k++) {
				children_lps[k] = current.neighbor_prior(k);
			}
			
			auto idx = arg_max_int(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
			current.expand_to_neighbor(idx);
			c = &c->children[idx];
		}
		
		c->process_evaluable(current);
		co_yield current;
    }
};
