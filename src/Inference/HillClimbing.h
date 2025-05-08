# pragma once

#include "TopN.h"
#include "SampleStreams.h"

/**
 * @class HillClimbing
 * @author Steven Piantadosi
 * @date 07/07/21
 * @file HillClimbing.h
 * @brief This cute kind of inference keeps a body of N top hypotheses, and successively proposes to them using standard proposals. 
 * 		  When it hasn't improved in ctl.restart steps, it will restart (resample). Note that this really
 *        should use ctl.restart>0, or else you just climb up one hill. 
 * 		  NOTE: We could propose propotional to each hypothesis' posterior, but it doesn't matter much
 *              because the best gets most probability mass (that's why we don't store more than n=1 
 *              defaultly)
 */

template<typename HYP>
class HillClimbing {
public:

	TopN<HYP> top;
	size_t N; // how big is top?
	size_t inner_samples;
	typename HYP::data_t data;
	double T = 1.0; 
		
	HillClimbing(HYP& h0, typename HYP::data_t d, size_t n=1, size_t is=100) :  N(n), inner_samples(is), data(d) {
		top.set_size(N);	
		
		// add this h0
		h0.compute_posterior(data);
		top << h0; 
	}
	
	generator<HYP&> run(Control ctl) {
		
		if(ctl.restart == 0) {
			CERR "# Warning: HillClimbing without restarts is probably a bad idea because it will only climb once." ENDL;
		}
		
		size_t samples = 0;
		size_t steps_since_improvement = 0;
		
		ctl.start();		
		while(ctl.running()) {
			if(ctl.steps > 0 and samples > ctl.steps) break; 
			
			// if we restart or if top is empty, we need to fill it up
			if(top.empty() or (ctl.restart>0 and steps_since_improvement > ctl.restart)) {
				[[unlikely]];
				
				steps_since_improvement = 0; // reset the couter
				
				while(true) { // avoid -infs
					auto current = top.best().restart();
					//CERR "# Restarting " TAB current.string() ENDL;
					current.compute_posterior(data);
					if(current.posterior == -infinity) continue; 
					
					co_yield current; 
					
					TopN<HYP> newTop(N);	
					newTop << current;
					top = std::move(newTop);
					break;
				}	
				continue;
			}				
			
			//CERR "# Top best: " TAB top.best().posterior TAB top.best().string() ENDL;
			
			TopN<HYP> newTop(N);
			//auto z = top.Z(T); // use this temperature
			for(auto& h : top.sorted(false)) {
				
				newTop << h; 
				
				// compute how many samples to take 
				// let's always do 1 to anything we store since otherwise we don't explore much
				//size_t mysamples = 1+inner_samples * exp(h.at_temperature(T) - z); // Do we want +1 here??
				
				size_t mysamples = std::ceil(inner_samples / double(N)); // equal distribution
				
				// since we are going best to worst, if we have zero sample we can stop
				if(mysamples == 0) break; 
				
				//CERR "# Proposing to " TAB h.posterior TAB mysamples TAB h.string() ENDL;
				
				for(size_t i=0;i<mysamples;i++) { // TODO: divide these proposals up uniformly to nodes...
					if(not ctl.running()) break; 
					if(ctl.steps > 0 and samples++ > ctl.steps) break; 
					
					// now propose to this
					auto pr = h.propose();			
					if(pr) { 
						
						auto [proposal, fb] = pr.value();
						// A lot of proposals end up with the same function, so if so, save time by not
						// computing the posterior
						if(proposal != h) {
							proposal.compute_posterior(data);
							
							if(proposal.posterior > -infinity)
								newTop << proposal;
								
							co_yield proposal;
						}
										
						if(proposal.posterior > top.best().posterior) {
							steps_since_improvement = 0;
						}
						else {
							steps_since_improvement++;
						}
						
					}
				}
			}
				
			// and move it over
			top = std::move(newTop);
		}
	}
	
};



