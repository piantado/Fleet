#pragma once

#include <thread>

//#define DEBUG_CHAINPOOL

#include <vector>

#include "Errors.h"
#include "MCMCChain.h"
#include "Timing.h"
#include "ParallelInferenceInterface.h"

/**
 * @class ChainPool
 * @author steven piantadosi
 * @date 29/01/20
 * @file ChainPool.h
 * @brief A ChainPool stores a bunch of MCMCChains and allows you to run them serially or in parallel. 
 * 		  NOTE: When you use a ChainPool, the results will not be reproducible with seed because timing determines when you
 *        switch chains. 
 */
template<typename HYP, typename callback_t>
class ChainPool : public ParallelInferenceInterface<> { 
	
public:
	std::vector<MCMCChain<HYP,callback_t>> pool;
	
	// these parameters define the amount of a thread spends on each chain before changing to another
	// NOTE: these interact with ParallelTempering swap/adapt values (because if these are too small, then
	// we won't have time to update every chain before proposing more swaps)
	static const unsigned long steps_before_change = 0;
	static const time_ms time_before_change = 200; 
	
	ChainPool() {}
	
	ChainPool(HYP& h0, typename HYP::data_t* d, callback_t& cb, size_t n, bool allcallback=true) {
		for(size_t i=0;i<n;i++) {
			if(allcallback or i==0) pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d, cb));
			else                    pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d));
		}
	}
	
	/**
	 * @brief This run helper is called internally by multiple different threads, and runs a given pool.
	 * @param ctl
	 */
	void run_thread(Control ctl) override {
		
		while( ctl.running() ) {
			int index = next_index() % pool.size();
			auto& chain = pool[index];
			
			//#ifdef DEBUG_CHAINPOOL
			//			COUT "# Running thread " <<std::this_thread::get_id() << " on "<< idx TAB chain.current.posterior TAB chain.current.string() ENDL;
			//#endif

			// we need to store how many samples we did 
			// so we can keep track of our total number
			auto old_samples =  chain.samples;
			
			// now run that chain -- TODO: Can update this to be more precise by running 
			// a set number of samples computed from steps and old_samples
			chain.run(Control(steps_before_change, time_before_change));
			
			// now update ctl's number of steps (since it might have been anything
			ctl.done_steps += chain.samples - old_samples; // add up how many we've done
						
			#ifdef DEBUG_CHAINPOOL
						COUT "# Thread " <<std::this_thread::get_id() << " stopping chain "<< idx TAB "at " TAB chain.current.posterior TAB chain.current.string() ENDL;
			#endif
						
		}
	}
	
};