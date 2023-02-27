#pragma once 

//#define DEBUG_CONTROL

#include <signal.h>

#include "FleetArgs.h"
#include "Timing.h"
#include "IO.h"

extern volatile sig_atomic_t CTRL_C;

/**
 * @class Control
 * @author Steven Piantadosi
 * @date 17/08/20
 * @file Control.h
 * @brief This class has all the information for running MCMC or MCTS in a little package. It defaultly constructs (via Control()) to read these from FleetArgs, which 
 * 		  are a bunch of variables set by Fleet::initialize from the command line. This makes it especially convenient to call with command line arguments, but others
 *        can be specified as well. 
 */

struct Control {
	unsigned long steps;
	time_ms runtime;
	size_t nthreads;
	unsigned long restart;
	
	timept start_time;
	
	
	// NOTE TODO: THE BELOW SHOULD BE UPDATED TO BE ATOMIC SINCE ITS ACCESSED BY MULTPLE THREADS 
	std::atomic<unsigned long> done_steps; // how many have we done -- updated by multiple threads

	bool break_CTRLC; // should we break on ctrl_c?

	Control(unsigned long st=FleetArgs::steps, 
			unsigned long t=FleetArgs::runtime,
			size_t thr=FleetArgs::nthreads, 
			unsigned long re=FleetArgs::restart) : 
					steps(st), runtime(t), nthreads(thr), restart(re), break_CTRLC(true) {
		// We defaultly read arguments from FleetArgs
		
		start(); // just defaultly because it's easier
	}
	
	Control(const Control& c)  : steps(c.steps), runtime(c.runtime), nthreads(c.nthreads), restart(c.restart), break_CTRLC(true) {		
		
		// NOTE: this is weird if using multiple threads 
		done_steps = c.done_steps.load();
	}
	
	void start() {
		/**
		 * @brief Start running
		 */		
		done_steps = 0;
		start_time = now();
	}
	

	bool running() {
		/**
		 * @brief Check if we are currently running. 
		 * @return 
		*/		
		
		++done_steps;
		
		if(break_CTRLC and CTRL_C) {
			#ifdef DEBUG_CONTROL
				std::cerr << "Control break on CTRL_C"  << std::endl;
			#endif			
			return false; 
		}
		
		if(steps > 0 and done_steps >= steps+1) {
			#ifdef DEBUG_CONTROL
				std::cerr << "Control break on steps"  << std::endl;
			#endif			
			return false;
		}
		
		if(runtime > 0 and time_since(start_time) >= runtime) {
			#ifdef DEBUG_CONTROL
				std::cerr << "Control break on runtime\t"<< runtime << std::endl;
			#endif	
			return false;
		}
			
		return true;			
	}
	
};

/**
 * @brief Make a Control object (NOTE it's a Control object not an InnerControl one) that has default 
 *        parameters of FleetArgs::inner_* 
 * @return 
 */
Control InnerControl(unsigned long st=FleetArgs::inner_steps, 
					 unsigned long t=FleetArgs::inner_runtime, 
					 size_t thr=1, 
					 unsigned long re=FleetArgs::inner_restart) {
	return {st,t,thr,re};
}