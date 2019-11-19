#pragma once

#include <thread>

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
	static const unsigned long time_before_change  = 1; 
	
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
							 unsigned long steps, 
							 unsigned long time) {
		
		std::function<size_t(size_t)> next_index = [running](const size_t frm) -> size_t {
			// assuming running is locked, find teh first not running
			for(size_t o=0;o<running->size();o++) {
				size_t idx = (o+frm+1)%running->size(); // offset of 1 so we start at frm+1
				if(not (*running)[idx]) {
					return idx;
				}
			}
			// should not get here if we have fewer threads than chains
			assert(0 && "*** We should only get here if running is out of sync (very bad) or you got here with more threads than chains, which should have been caught.");
		};						 
								 
		// run_helper looks at the number of chains and number of threads and runs each for swap_steps and swap_time
		auto          my_time  = now();
		unsigned long my_steps = 0;
		size_t idx = 0; // what pool item am I running on?
		
		while( (steps == 0 or my_steps <= steps)          and 
			   (time  == 0 or time_since(my_time) < time) and 
			   !CTRL_C) {
					
//			CERR "# Running thread " <<std::this_thread::get_id() << " on "<< idx ENDL;

			do {
				// find the next running we can update and do it
				std::lock_guard lock(*running_mutex);
				(*running)[idx] = false;	
				idx = next_index(idx);
				(*running)[idx] = true;
			} while(0);		
			
			// we need to store how many samples we did 
			// so we can keep track of our total number
			auto old_samples = (*pool)[idx].samples; 
			
			// now run that chain -- TODO: Can update this to be more precise by running 
			// a set number of samples computed from steps and old_samples
			(*pool)[idx].run(steps_before_change, time_before_change);
			
			// pick the next index (and free mine)
			my_steps += (*pool)[idx].samples - old_samples; // add up how many we've done
		}
		
		// and unlock this
		std::lock_guard lock(*running_mutex);
		(*running)[idx] = false;	
	}
	
	virtual void run(unsigned long steps, unsigned long time) {
		
		if(nthreads > pool.size()){
			CERR "# Warning: more threads (" << nthreads << ") than chains ("<<pool.size()<<") is probably dumb.";
			assert(0); // let's not allow it for now -- make __run_helper much more complex
		}
		
		std::thread threads[nthreads]; 
		std::vector<bool> running(pool.size()); // what chain is running?

		for(unsigned long i=0;i<pool.size();i++) {
			running[i] = false;
		}
		
		for(unsigned long t=0;t<nthreads;t++) {
			threads[t] = std::thread(this->__run_helper, &pool, &running, &running_mutex, steps, time);
		}
		
		// wait for all to complete
		for(unsigned long t=0;t<nthreads;t++) {
			threads[t].join();
		}
	}
	
	
};