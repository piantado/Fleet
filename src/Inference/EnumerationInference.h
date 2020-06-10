#pragma once


#include "Enumeration.h"
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
template<typename HYP, typename Grammar_t, typename callback_t>
class EnumerationInference : public ParallelInferenceInterface {

public:
	
	Grammar_t* grammar;
	nonterminal_t start; 
	typename HYP::data_t* data;
	callback_t* callback; // must be static so it can be accessed in GraphNode
	
	
	EnumerationInference(Grammar_t* g, nonterminal_t st, typename HYP::data_t* d, callback_t& cb) : 
		grammar(g), start(st), data(d), callback(&cb) {
	}
	
	void run_thread(Control ctl) override {

		ctl.start();
		while(ctl.running()) {
			enumerationidx_t nxt = (enumerationidx_t) next_index();
			auto n = expand_from_integer(grammar, start, nxt);
			HYP h(grammar, n);
			h.compute_posterior(*data);
			(*callback)(h);
		}
	}
};