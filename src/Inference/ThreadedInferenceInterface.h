#pragma once 


#include <atomic>
#include <mutex>
#include <thread>

#include "Control.h"
#include "SampleStreams.h"
#include "ConcurrentQueue.h"

/**
 * @class ThreadedInferenceInterface
 * @author piantado
 * @date 07/06/20
 * @file ThreadedInferenceInterface.h
 * @brief This manages multiple threads for running inference. This requires a subclass to define run_thread, which 
 * 			is what each individual thread should do. All threads can then be called with run(Control, Args... args), which
 * 			copies the control for each thread (setting threads=1) and then passes the args arguments onward
 */
 template<typename X, typename... Args>
class ThreadedInferenceInterface {
public:

	// Subclasses must implement run_thread, which is what each individual thread 
	// gets called on (and each thread manages its own locks etc)
	virtual generator<X&> run_thread(Control& ctl, Args... args) = 0;
	
	// index here is used to index into larger parallel collections. Each thread
	// is expected to get its next item to work on through index, though how will vary
	std::atomic<size_t> index; 
	
	// How many threads? Used by some subclasses as asserts
	size_t __nthreads; 
	std::atomic<size_t> __nrunning;// everyone updates this when they are done
	
	ConcurrentQueueRing<X> to_yield;

	ThreadedInferenceInterface() : index(0), __nthreads(0),  __nrunning(0), to_yield(FleetArgs::nthreads) { }
	
	/**
	 * @brief Return the next index to operate on (in a thread-safe way).
	 * @return 
	 */	
	unsigned long next_index() { return index++; }
	
	/**
	 * @brief How many threads are currently run in this interface? 
	 * @return 
	 */
	size_t nthreads() {	return __nthreads; }
	
	/**
	 * @brief We have to wrap run_thread in something that manages the sync with main. This really just
	 *        synchronizes the output of run_thread with run below. NOTE this makes a copy of x into
	 *        the local next_x, so that when the thread keeps running, it doesn't mess anything up. We
	 *        may in the future block the thread and return a reference, but its not clear that's faster
	 * @param ctl
	 */	
	void run_thread_generator_wrapper(size_t thr, Control& ctl, Args... args) {
		
		for(auto& x : run_thread(ctl, args...)) {
			if(CTRL_C) break;
			
			if(x.born_chain_idx == 0 or not FleetArgs::yieldOnlyChainOne) {
				to_yield.push(x, thr);
			}
			
			
		}
		
		// we always notify when we're done, after making sure we're not running or else the
		// other thread can block
		__nrunning--;
	}	
	
	/**
	 * @brief Set up the multiple threads and actually run, calling run_thread_generator_wrapper
	 * @param ctl
	 * @return 
	 */	
	generator<X&> run(Control ctl, Args... args) {
		
		std::vector<std::thread> threads(ctl.nthreads); 
		__nthreads = ctl.nthreads; // save this for children
		assert(__nrunning==0);
		
		// Make a new control to run on each thread and then pass this to 
		// each subthread. This way multiple threads all share the same control
		// which is required for getting an accurate total count
		Control ctl2  = ctl; 
		ctl2.nthreads = 1;
		ctl2.start(); 

		// give this just some extra space here
		//to_yield.resize(FleetArgs::MCMC_QUEUE_MULTIPLIER*ctl.nthreads); // just some extra space here

		// start each thread
		for(unsigned long thr=0;thr<ctl.nthreads;thr++) {
			++__nrunning;
			threads[thr] = std::thread(&ThreadedInferenceInterface<X, Args...>::run_thread_generator_wrapper, this, thr, std::ref(ctl2), args...);
		}
	
		// now yield as long as we have some that are running
		while(__nrunning > 0 and !CTRL_C) { // we don't want to stop when its empty because a thread might fill it
//			if(not to_yield.empty()) { // w/o this we might pop when its empty...
//				//print((size_t)to_yield.push_idx, (size_t)to_yield.pop_idx, to_yield.size(), to_yield.N);
//				co_yield to_yield.pop();
//			}
//			print("TII", to_yield.empty());
			if(not to_yield.empty())  {
				auto val = to_yield.pop(); // search through until we find one
				
				if(val.has_value())	
					co_yield val.value();
				else                
					break;
			}
			
		}
		
		// now we're done filling but we still may have stuff in the queue
		// some threads may be waiting so we can't join yet. 
		while(not to_yield.empty()) {
			auto val = to_yield.pop(); // search through until we find one
			if(val.has_value())	
				co_yield val.value();
			else                
				break; // NOTE: we migth break here and leave some in queue -- but we only get break on CTRL_C
		}
		
		// wait for all to complete
//		print("waiting");
//		for(unsigned long thr=0;thr<ctl.nthreads;thr++) {
//			threads[thr].join();
//			print(thr, "joined");
//		}
		for(auto& t : threads) 
			t.join();
//		print("done");
			
	}
	
	generator<X&> unthreaded_run(Control ctl, Args... args) {

		// This is a simple version where we don't use threads -- useful 
		// because it's hard to debug otherwise 
		
		std::cerr << "*** Warning running unthreaded_run (intended for debugging)" << std::endl;
		ctl.start();
		for(auto& x : run_thread(ctl, args...)) {
			if(CTRL_C) break; // must come first or else we yield something invalid
			
			auto val = to_yield.pop(); // search through until we find one
			if(val.has_value())	co_yield val.value();	
			else                break;
		}		
	}
	
};


