#pragma once


#include "BasicEnumeration.h"
#include "Control.h"
#include <queue>
#include <thread>

#include "ParallelInferenceInterface.h"

/**
 * @class EnumerationInference
 * @author piantado
 * @date 09/06/20
 * @file EnumerationInference.h
 * @brief Enumeration inferences enumerates hypotheses using a counting algorithm from the grammar. It supports multithreading
 * 		  and requires a hypothesis, grammar, start type, and callback.
 */
template<typename HYP, typename Grammar_t, typename callback_t, typename Enumerator>
class EnumerationInference : public ParallelInferenceInterface<> {

public:
	
	Grammar_t* grammar;
	typename HYP::data_t* data;
	callback_t* callback; // must be static so it can be accessed in GraphNode
	
	
	EnumerationInference(Grammar_t* g, typename HYP::data_t* d, callback_t& cb) : 
		grammar(g), data(d), callback(&cb) {
	}
	
	void run_thread(Control ctl) override {
	
		Enumerator ge(this->grammar);
		
		ctl.start();
		while(ctl.running()) {
			enumerationidx_t nxt = (enumerationidx_t) next_index();
			auto n = ge.toNode(nxt, grammar->start());
			auto h = MyHypothesis(grammar, n); // don't call make -- that restarts
			h.compute_posterior(*data);

			if(callback != nullptr and nxt >= ctl.burn and
				(ctl.thin == 0 or FleetStatistics::enumeration_steps % ctl.thin == 0)) {
				(*callback)(h);
			}
			
			if(ctl.print > 0 and FleetStatistics::enumeration_steps % ctl.print == 0) {
				h.print();
			};
			
			++FleetStatistics::enumeration_steps;
		}
	}
};