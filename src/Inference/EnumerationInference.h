#pragma once


#include "Enumeration.h"
#include "Control.h"
#include <queue>
#include <thread>

#include "ParallelInferenceInterface.h"

template<typename HYP, typename Grammar_t, typename callback_t>
class EnumerationInference : ParallelInferenceInterface {

public:
	
	typename HYP::data_t* data;
	Grammar_t* grammar;
	callback_t* callback; // must be static so it can be accessed in GraphNode

	// make this atomic so all threads can access
	std::atomic<enumerationidx_t> current_index;
	
	EnumerationInference(Grammar_t* g, typename HYP::data_t* d, callback_t& cb) : grammar(g), data(d), callback(&cb), current_index(0) {
	}
	
	void run_thread(Control ctl) override {

		ctl.start();
		while(ctl.running()) {
			enumerationidx_t nxt = current_index++;
			auto n = expand_from_integer(&grammar, grammar.nt<S>(), z); 
			HYP h(&grammar,n);
			h.compute_posterior(mydata);
			(*callback)(h);
		}
	}
	
}