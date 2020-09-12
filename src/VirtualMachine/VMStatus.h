#pragma once 

// This class stores the possible statuses of a VirtualMachine
enum class vmstatus_t {
	GOOD=0,  
	COMPLETE=1, 
	ERROR, 
	RECURSION_DEPTH, 
	RANDOM_CHOICE, 
	RANDOM_CHOICE_NO_DELETE, 
	SIZE_EXCEPTION, 
	RUN_TOO_LONG, 
	OP_ERR_ABORT, 
	RANDOM_BREAKOUT
}; 

