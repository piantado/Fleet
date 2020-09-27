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
	unsigned long burn;
	unsigned long restart;
	unsigned long thin;
	unsigned long print; 
	
	timept start_time;
	unsigned long done_steps; // how many have we done?

	bool break_CTRLC; // should we break on ctrl_c?

	Control(unsigned long st=FleetArgs::steps, 
			unsigned long t=FleetArgs::runtime,
			size_t thr=FleetArgs::nthreads, 
			unsigned long bu=FleetArgs::burn, 
			unsigned long re=FleetArgs::restart, 
			unsigned long th=FleetArgs::thin, 
			unsigned long pr=FleetArgs::print) : 
					steps(st), runtime(t), nthreads(thr), burn(bu), restart(re), thin(th), print(pr), break_CTRLC(true) {
		// We defaultly read arguments from FleetArgs
		
		start(); // just defaultly because it's easier
	}
	
	void start() {
		/**
		 * @brief Start running
		 */
		
		done_steps = 0;
		start_time = now();
	}
	
	// Add some functions here to chcek these
//	bool burn() {  return done_steps < burn; }
//	bool print() { 
	
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
		
		if(steps > 0 and done_steps >= steps) {
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
