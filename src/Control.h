#pragma once 

//#define DEBUG_CONTROL

/**
* @class Control
* @author steven piantadosi
* @date 03/02/20
* @file Control.h
* @brief This bundles together information for running MCMC or MCTS, including number of steps, amount of time, etc.
* NOTE: In general this should NOT be passed by reference because we want start_time to be the time we started the function it is passed to (start time is the time of construction, here)
*/ 

#include "IO.h"

struct Control {
	// Parameters for running MCMC or MCTS
	// 
	unsigned long steps;
	time_ms time;
	size_t threads;
	unsigned long burn;
	unsigned long thin; 
	unsigned long restart;
	
	timept start_time;
	unsigned long done_steps; // how many have we done?

	bool break_CTRLC; // should we break on ctrl_c?

	Control(unsigned long s=0, time_ms t=0, size_t thr=1, unsigned long r=0) : steps(s), time(t), threads(thr), burn(0), 
					thin(0), restart(r), done_steps(0), break_CTRLC(true) {
		start(); // just defaultly because it's easier
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
		
		if(steps > 0 and done_steps > steps) {
			#ifdef DEBUG_CONTROL
				std::cerr << "Control break on steps"  << std::endl;
			#endif			
			return false;
		}
		
		if(time > 0 and time_since(start_time) > time) {
			#ifdef DEBUG_CONTROL
				std::cerr << "Control break on time\t"<< time << std::endl;
			#endif	
			return false;
		}
			
		return true;			
	}
	
};
