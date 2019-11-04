#pragma once

#include <thread>

template<typename HYP, typename callback_t>
class ChainPool { 
	// Run n chains in parallel to set up a parallel scheme
	
public:
	std::vector<MCMCChain<HYP,callback_t>> pool;
	std::mutex running_mutex; // modify index of who is running
	
	// these parameters define the amount of a thread spends on each chain before changing to another
	static const unsigned long steps_before_change = 0;
	static const unsigned long time_before_change  = 1; 
	
	ChainPool() { }
	
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
		
		// A helper function to get the lock and get the next pool thats not running anymore
		// starting from frm+1 
		std::function<size_t(size_t)> next_index = [running_mutex,running](const size_t frm) -> size_t {
			std::lock_guard lock(*running_mutex);
			
			// for simplicity, we'll update this here
			(*running)[frm] = false;			
			
			// and then find the next
			for(size_t o=0;o<running->size();o++) {
				size_t idx = (o+frm+1)%running->size(); // offset of 1 so we start at frm+1
				if(not (*running)[idx]) {
					(*running)[idx] = true;
					return idx;
				}
			}
			// should not get here if we have fewer threads than chains
			assert(0);
		};						 
								 
		// run_helper looks at the number of chains and number of threads and runs each for swap_steps and swap_time
		auto          my_time  = now();
		unsigned long my_steps = 0;
		size_t idx = next_index(0); // what pool item am I running on?
		
		while(my_steps <= steps and time_since(my_time) < time and !CTRL_C) {			
			CERR "# Running thread " <<std::this_thread::get_id() << " on "<< idx ENDL;
			
			(*pool)[idx].run(steps_before_change, time_before_change);
			
			// pick the next index (and free mine)
			idx = next_index(idx);
			my_steps += steps_before_change;
		}
			
		std::lock_guard lock(*running_mutex);
		(*running)[idx] = false;	
	}
	
	virtual void run(unsigned long steps, unsigned long time) {
		
		assert(nthreads <= pool.size() && "*** Should not run with more threads than chains.");
		
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