#pragma once

#include <thread>

//#define DEBUG_PARTITION_MCMC 1

#include <vector>

#include "Errors.h"
#include "MCMCChain.h"
#include "Timing.h"
#include "ParallelInferenceInterface.h"

/**
 * @class PartitionMCMC
 * @author Steven Piantadosi
 * @date 26/12/20
 * @file PartitionMCMC.h
 * @brief This takes a grammar and expands to a given depth, and then runs a separate MCMC chain in each 
 * 		  partial subtree that is generated. 
 */
template<typename HYP, typename callback_t>
class PartitionMCMC : public ChainPool<HYP, callback_t> { 
public:
	
	PartitionMCMC(HYP& h0, size_t depth, typename HYP::data_t* data, callback_t& callback) {
		std::set<HYP> cur = {h0};
		std::set<HYP> nxt;
		
		for(size_t dd=0;dd<depth;dd++) {
			for(auto& h: cur) {
				auto neigh = h.neighbors();
				for(int n=0;n<neigh;n++) {
					auto newh = h;
					newh.expand_to_neighbor(n);
					
					// now we need to check here and make sure that there aren't any complete trees
					// because if there are, we won't include them in any trees below
					if(newh.is_evaluable()) {
						newh.compute_posterior(*data);
						callback(newh);
					}
					else {
						nxt.insert(newh);
					}
				}
			}
			cur = nxt;
		}
		
		CERR cur.size() ENDL;
		
		// now make an MCMC chain on each of these
		// (all must use the callback)
		for(auto& h: cur) {
			HYP x = h;
			for(auto& n : x.get_value() ){
				n.can_resample = false;
			}

			x.complete(); // fill in any structural gaps
		
			this->pool.push_back(MCMCChain<HYP,callback_t>(x, data, callback));
			
			#ifdef DEBUG_PARTITION_MCMC
				COUT "Starting PartitionMCMC on " << x.string() ENDL;
			#endif
		}
		
	}
	

};	