#pragma once 

#include "MCTSBase.h"

/**
 * @class PartialMCTSNode
 * @author piantado
 * @date 03/07/20
 * @file MCTS.h
 * @brief This is a version of MCTS that plays out children nplayouts times instead of expanding a full tree
 */
template<typename this_t, typename HYP>
class PartialMCTSNode : public MCTSBase<this_t,HYP> {
	using Super = MCTSBase<this_t,HYP>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	
	virtual generator<HYP&> search_one(HYP& current) override {
		if(DEBUG_MCTS) DEBUG("PartialMCTSNode SEARCH ONE ", this, "\t["+current.string()+"] ", this->nvisits);
	
		auto c = this->descend_to_childless(current); //sets current and returns the node. 
		
		// if we are a terminal 
		if(current.is_evaluable()) {
			c->process_evaluable(current);
			co_yield current;			
		}
		else {
			// else add the next row of children and choose one to playout
			c->add_children(current); 
			auto idx = c->sample_child_index(current);
			current.expand_to_neighbor(idx); 
			for(auto& h : this->playout(current)) { // the difference is that here, we call playout instead of search_one
				co_yield h;
			}
		}
    }

	/**
	 * @brief This gets called on a child that is unvisited. Typically it would consist of filling in h some number of times and
	 * 		  saving the stats
	 * @param h
	 */
	virtual generator<HYP&> playout(HYP& current) {
		if(current.is_evaluable()) {
			this->process_evaluable(current);
			co_yield current;
		}
		else {
			PriorInference samp(current.grammar, this->data, &current);
			double mx = -infinity;
			for(auto& h : samp.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart))){
				mx = std::max(mx, h.posterior);
				co_yield h;
			}
			this->add_sample(mx); // this stores our value
		}
	}	
};

