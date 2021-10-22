#pragma once

#include <thread>

//#define DEBUG_PARTITION_MCMC 1

#include <vector>

#include "Errors.h"
#include "MCMCChain.h"
#include "Timing.h"
#include "ThreadedInferenceInterface.h"

/**
 * @brief Compute "partitions" meaning trees with holes where we can run MCMC on the hole. This takes
 *        either a max depth or max number of partitions. NOTE here that anything which is a complete
 *        tree that gets evaluated is NOT included
 * @param h0
 * @param max_depth
 * @param max_size
 * @return 
 */
template<typename HYP>
std::set<HYP> get_partitions(HYP& h0, const size_t max_depth, const size_t max_size) {
	assert(not (max_depth==0 and max_size==0));
	
	std::set<HYP> cur = {h0};
	std::set<HYP> nxt;
		
	for(size_t dd=0;(max_depth==0 or dd<max_depth) and (max_size==0 or cur.size() < max_size);dd++) {
		for(auto& h: cur) {
			auto neigh = h.neighbors();
			for(int n=0;n<neigh;n++) {
				auto newh = h;
				newh.expand_to_neighbor(n);
				PRINTN(h.string(), newh.string(), n, dd, max_depth);
				
				// now we need to check here and make sure that there aren't any complete trees
				// because if there are, we won't include them in any trees below
				if(not newh.is_evaluable()) {
					nxt.insert(newh);
				}
			}
		}
		
		// if we made too many, return what we had, else keep going
		if(max_size > 0 and nxt.size() > max_size){
			return cur; 
		}
		else {
			cur = nxt;
		}
	}
	
	return cur;
}


/**
 * @class PartitionMCMC
 * @author Steven Piantadosi
 * @date 26/12/20
 * @file PartitionMCMC.h
 * @brief This takes a grammar and expands to a given depth, and then runs a separate MCMC chain in each 
 * 		  partial subtree that is generated. 
 */
template<typename HYP>
class PartitionMCMC : public ChainPool<HYP> { 
public:
	
	/**
	 * @brief This specifies both a max_Size and a max_depth which are the most partitions we'll make. If max_size=0
	 *        then we ignore it
	 * @param h0
	 * @param max_depth
	 * @param data
	 * @param max_size
	 */
	PartitionMCMC(HYP& h0, size_t max_depth, typename HYP::data_t* data, size_t max_size=0) {

		auto cur = get_partitions(h0, max_depth, max_size);
		
		
		// now make an MCMC chain on each of these
		// (all must use the callback)
		for(auto& h: cur) {
			HYP x = h;
			for(auto& n : x.get_value() ){
				n.can_resample = false;
			}

			x.complete(); // fill in any structural gaps
		
			this->pool.push_back(MCMCChain<HYP>(x, data));
			this->running.push_back(ChainPool<HYP>::RunningState::READY);
			
			#ifdef DEBUG_PARTITION_MCMC
				COUT "Starting PartitionMCMC on " << x.string() ENDL;
			#endif
		}
		
	}
	

};	