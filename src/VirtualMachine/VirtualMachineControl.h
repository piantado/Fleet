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
	
};

// The defaults:
unsigned long VirtualMachineControl::MAX_RECURSE = 64;
unsigned long VirtualMachineControl::MAX_RUN_PROGRAM = 1024;

unsigned long VirtualMachineControl::MAX_STEPS = 512;
unsigned long VirtualMachineControl::MAX_OUTPUTS = 512;
double VirtualMachineControl::MIN_LP = -10;