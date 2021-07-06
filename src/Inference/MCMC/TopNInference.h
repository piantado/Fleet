# pragma once

// This is inspired by rational rules MCMC -- we store the top hypotheses and propose to them
// and at each iteration keep storing the top and proposing. This allows us to take the
// expected behavior of MCMC (time on a hypothesis ~ posterior probability) and make it true
// on every iteration 

// TODO: Make each proposal work on a different node in the tree...

#include "TopN.h"
#include "Coroutines.h"

template<typename HYP>
class TopNInference {

public:

	TopN<HYP> top;
	size_t N; // how big is top?
	size_t inner_samples;
	typename HYP::data_t* data;
	double T = 1.0; 
		
	TopNInference(HYP& h0, typename HYP::data_t* d, size_t n=1000, size_t is=1000) :  N(n), inner_samples(is), data(d) {
		top.set_size(N);	
		h0.compute_posterior(*data);
		top << h0; 
	}
	
	generator<HYP&> run(Control ctl) {
		
		size_t samples = 0;
		
		ctl.start();		
		while(ctl.running()) {
			
			TopN<HYP> newTop(N);
			
			auto z = top.Z(T);
			for(auto& h : top.values()) { // TODO: Should be able to break out when our samples are done
				
				newTop << h; 
				
				// compute how many samples to take 
				// let's always do 1 to anything we store since otherwise we don't explore much
				size_t mysamples = 1+inner_samples * exp(h.at_temperature(T) - z);				
			
				//CERR "# Proposing to " TAB h.posterior TAB mysamples TAB h.string() ENDL;
				
				for(size_t i=0;i<mysamples;i++) { // TODO: divide these proposals up uniformly to nodes...
					if(not ctl.running()) break; 
					if(samples++ > ctl.steps) break; 
					
					// now propose to this
					auto [proposal, fb] = h.propose();			
					
					// A lot of proposals end up with the same function, so if so, save time by not
					// computing the posterior
					if(proposal != h) {
						proposal.compute_posterior(*data);
						newTop << proposal;
						co_yield proposal;
					}
				}
			}
			
			// and move it over
			top = std::move(newTop);
		}
	}
	
};



