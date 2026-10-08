#pragma once

#include "Control.h"
#include <queue>
#include <thread>

#include "ThreadedInferenceInterface.h"

/**
 * @class PriorInference
 * @author piantado
 * @date 10/06/20
 * @file PriorInference.h
 * @brief Inference by sampling from the prior -- doesn't tend to work well, but might be a useful baseline
 */
template<typename HYP>
class PriorInference : public ThreadedInferenceInterface<HYP> {

public:
	std::vector<typename HYP::datum_t> data;
	HYP* from; // when we want to sample completions of a hypothesis
	
	PriorInference(std::vector<typename HYP::datum_t>& d, HYP* fr=nullptr) : 
		data(d), from(fr) {
	}
	
	generator<HYP&> run_thread(Control& ctl) override {

		ctl.start();
		while(ctl.running()) {
			HYP h;
			if(from != nullptr) {
				h = *from; // copy 
				h.complete();
			}
			else {
				h = MyHypothesis::sample();
			}
			h.compute_posterior(data);
			co_yield h;
		}
	}
};