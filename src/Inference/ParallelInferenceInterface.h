#pragma once 

#include <atomic>
#include <thread>
#include "Control.h"

/**
 * @class InfereceInterface
 * @author piantado
 * @date 07/06/20
 * @file InferenceInterface.h
 * @brief This manages multiple threads for running inference. This requires a subclass to define run_thread, which 
 * 			is what each individual thread should do. All threads can then be called with run(Control, Args... args), which
 * 			copies the control for each thread (setting threads=1) and then passes the args arguments onward
 */
 template<typename... Args>
class ParallelInferenceInterface {
public:

	// Subclasses must implement run_thread, which is what each individual thread 
	// gets called on (and each thread manages its own locks etc)
	virtual void run_thread(Control ctl, Args... args) = 0;
	
	// index here is used to index into larger parallel collections or enumerate. Each thread
	// is expected to get its next item to work on through index, though how will vary
	std::atomic<unsigned long> index; 
	
	
	ParallelInferenceInterface() : index(0) {
		
	}
	
	/**
	 * @brief Return the next index to operate on (in a thread-safe way).
	 * @return 
	 */	
	unsigned long next_index() {
		return index++;
	}
	
	/**
	 * @brief Run is the main control interface. Copies of ctl get made and passed to each thread in run_thread. 
	 * @param ctl
	 */	
	void run(Control ctl, Args... args) {
		
		std::vector<std::thread> threads(ctl.threads); 

		for(unsigned long t=0;t<ctl.threads;t++) {
			Control ctl2 = ctl; ctl2.threads=1; // we'll make each thread just one
			threads[t] = std::thread(&ParallelInferenceInterface<Args...>::run_thread, this, ctl2, args...);
		}
		
		// wait for all to complete
		for(unsigned long t=0;t<ctl.threads;t++) {
			threads[t].join();
		}
	}
	
};