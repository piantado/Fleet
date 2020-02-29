#pragma once

#include <thread>

//#define DEBUG_CHAINPOOL

/**
 * @class ChainPool
 * @author steven piantadosi
 * @date 29/01/20
 * @file ChainPool.h
 * @brief A ChainPool stores a bunch of MCMCChains and allows you to run them serially or in parallel. 
 */
template<typename HYP, typename callback_t>
class ChainPool { 
	// Run n chains in parallel to set up a parallel scheme
	
public:
	std::vector<MCMCChain<HYP,callback_t>> pool;
	std::mutex running_mutex; // modify index of who is running
	
	// these parameters define the amount of a thread spends on each chain before changing to another
	// NOTE: these interact with ParallelTempering swap/adapt values (because if these are too small, then
	// we won't have time to update every chain before proposing more swaps)
	static const unsigned long steps_before_change = 0;
	static const time_ms time_before_change = 250; 
	
	ChainPool() {}
	
	ChainPool(HYP& h0, typename HYP::t_data* d, callback_t& cb, size_t n, bool allcallback=true) {
		for(size_t i=0;i<n;i++) {
			if(allcallback or i==0) pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d, cb));
			else                    pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d));
		}
	}
	

	static void __run_helper(std::vector<MCMCChain<HYP,callback_t>>* pool, 
							 std::vector<bool>* running, 
							 std::mutex* running_mutex,
							 Control ctl) {
		/**
		 * @brief This run helper is called internally by multiple different threads, and runs a given pool. 
		 * @param ctl
		 */
								 
		// NOTE: There are two controls here -- one for the outer loop (which operates on different chains)
		// and one for the inner loop 
		
		std::function<size_t(size_t)> next_index = [running](const size_t frm) -> size_t {
			// assuming running is locked, find the first not running
			for(size_t o=0;o<running->size();o++) {
				size_t idx = (o+frm+1)%running->size(); // offset of 1 so we start at frm+1
				if(not (*running)[idx]) {
					return idx;
				}
			}
			// should not get here if we have fewer threads than chains
			assert(0 && "*** We should only get here if running is out of sync (very bad) or you got here with more threads than chains, which should have been caught.");
		};						 
								 
		size_t idx = 0; // what pool item am I running on?
		while( ctl.running() ) {
		
			do {
				// find the next running we can update and do it
				std::lock_guard lock(*running_mutex);
				(*running)[idx] = false;	
				idx = next_index(idx);
				(*running)[idx] = true;

			} while(0);		

#ifdef DEBUG_CHAINPOOL
			COUT "# Running thread " <<std::this_thread::get_id() << " on "<< idx TAB (*pool)[idx].current.posterior TAB (*pool)[idx].current.string() ENDL;
#endif

			// we need to store how many samples we did 
			// so we can keep track of our total number
			auto old_samples = (*pool)[idx].samples; 
			
			// now run that chain -- TODO: Can update this to be more precise by running 
			// a set number of samples computed from steps and old_samples
			(*pool)[idx].run(Control(steps_before_change, time_before_change));
			
			// now update ctl's number of steps (since it might have been anything
			ctl.done_steps += (*pool)[idx].samples - old_samples; // add up how many we've done
		}
		
		// and unlock this
		std::lock_guard lock(*running_mutex);
		(*running)[idx] = false;	
	}
	
	virtual void run(Control ctl) {
		
		if(ctl.threads > pool.size()){
			CERR "# Warning: more threads (" << ctl.threads << ") than chains ("<<pool.size()<<") is probably dumb.";
			assert(0); // let's not allow it for now -- make __run_helper much more complex
		}
		
		std::vector<std::thread> threads(ctl.threads); 
		std::vector<bool> running(pool.size()); // what chain is running?

		for(unsigned long i=0;i<pool.size();i++) {
			running[i] = false;
		}
		
		for(unsigned long t=0;t<ctl.threads;t++) {
			Control ctl2 = ctl; ctl2.threads=1; // we'll make each thread just one
			threads[t] = std::thread(this->__run_helper, &pool, &running, &running_mutex, ctl2);
		}
		
		// wait for all to complete
		for(unsigned long t=0;t<ctl.threads;t++) {
			threads[t].join();
		}
	}
	
	
};