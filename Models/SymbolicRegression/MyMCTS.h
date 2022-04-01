#include "PartialMCTSNode.h"
#include "ParallelTempering.h"
#include "MCMCChain.h"

class MyMCTS : public PartialMCTSNode<MyMCTS, MyHypothesis> {
	using Super = PartialMCTSNode<MyMCTS, MyHypothesis>;
	using Super::Super;

public:
	MyMCTS(MyMCTS&&) { assert(false); } // must be defined but not used

	virtual generator<MyHypothesis&> playout(MyHypothesis& current) override {
		// define our own playout here to call our callback and add sample to the MCTS
		
		MyHypothesis h0 = current; // need a copy to change resampling on 
		for(auto& n : h0.get_value() ){
			n.can_resample = false;
		}

		h0.complete(); // fill in any structural gaps	
		
		// check to see if we have no gaps and no resamples, then we just run one sample. 
		std::function no_resamples = +[](const Node& n) -> bool { return not n.can_resample;};
		if(h0.count_constants() == 0 and h0.get_value().all(no_resamples)) {
			this->process_evaluable(current);
			co_yield h0;
		}
		else {
			
			double best_posterior = -infinity; 
			long steps_since_best = 0;
			
			// else we run vanilla MCMC
			MCMCChain chain(h0, data);
//			ParallelTempering chain(h0, &mydata, 10, 100.0 ); 
			for(auto& h : chain.run(InnerControl()) | burn(BURN_N) | thin(FleetArgs::inner_thin) ) {
				
				// return if we haven't improved in howevermany samples. 
				if(h.posterior > best_posterior) {
					best_posterior = h.posterior;
					steps_since_best = 0;
				}
				
				this->add_sample(h.posterior); 
				co_yield h;

				//
				if(steps_since_best > 100000) break; 
			}

		}
	}
	
};
