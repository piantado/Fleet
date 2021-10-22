#pragma once

#include <queue>
#include <thread>

#include "BasicEnumeration.h"
#include "Control.h"
#include "Coroutines.h"

#include "ThreadedInferenceInterface.h"

/**
 * @class EnumerationInference
 * @author piantado
 * @date 09/06/20
 * @file EnumerationInference.h
 * @brief Enumeration inferences enumerates hypotheses using a counting algorithm from the grammar. It supports multithreading
 * 		  and requires a hypothesis, grammar, and starting type.
 */
template<typename HYP, typename Grammar_t, typename Enumerator>
class EnumerationInference : public ThreadedInferenceInterface<HYP> {

public:
	
	Grammar_t* grammar;
	typename HYP::data_t* data;
		
	EnumerationInference(Grammar_t* g, typename HYP::data_t* d) : 
		grammar(g), data(d) {
	}
	
	generator<HYP&> run_thread(Control& ctl) override {
	
		Enumerator ge(this->grammar);
		
		ctl.start();
		while(ctl.running()) {
			enumerationidx_t nxt = (enumerationidx_t) this->next_index();
			auto n = ge.toNode(nxt, grammar->start());
			//PRINTN(n.string());
			
			MyHypothesis h(n); 
			h.compute_posterior(*data);

			co_yield h;

			++FleetStatistics::enumeration_steps;
		}
	}
};