#pragma once 

	
/**
 * @class VirtualMachinePoolControl
 * @author Steven Piantadosi
 * @date 06/09/20
 * @file VirtualMachinePool.h
 * @brief A little class that any VirtualMachinePool AND VirtualMachines inherit to control their behavior.
 * 		  NOTE That this is not passed around like Fleet::Control--it is just meant to be a place to set static variables
 * 		  And then it is inherited by VirtualMachinePool
 */
struct VirtualMachineControl {	
	
	///////////////////////////////////////
	// Variables for VirtualMachineState
	///////////////////////////////////////
	
	// this is the max number of times we can call recurse, NOT the max depth
	static unsigned long MAX_RECURSE; 
	
	// what is the longest I'll run a single program for? If we try to do more than this many ops, we're done
	static unsigned long MAX_RUN_PROGRAM; 
	
	///////////////////////////////////////
	// Variables for VirtualMachinePool
	///////////////////////////////////////
	
	// most number of VirtualMachine steps we'll take
	static unsigned long MAX_STEPS;
	
	// most outputs we'll accumulate
	static unsigned long MAX_OUTPUTS;
	
	// prune VirtualMachineStates with less than this log probability
	static double MIN_LP;
	
	// count up how often we break for different reasons (useful for seeing what bounds matter)
//	static unsigned long BREAK_output;
//	static unsigned long BREAK_steps;
//	static unsigned long BREAK_recurse;
	
	
};

// The defaults:
unsigned long VirtualMachineControl::MAX_RECURSE = 64;
unsigned long VirtualMachineControl::MAX_RUN_PROGRAM = 10000; // updated to have higher default

unsigned long VirtualMachineControl::MAX_STEPS = 512;
unsigned long VirtualMachineControl::MAX_OUTPUTS = 512;
double VirtualMachineControl::MIN_LP = -10;

//unsigned long VirtualMachineControl::BREAK_output = 0;
//unsigned long VirtualMachineControl::BREAK_steps = 0;
//unsigned long VirtualMachineControl::BREAK_recurse = 0;