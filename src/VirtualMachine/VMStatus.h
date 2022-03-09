#pragma once 

// This class stores the possible statuses of a VirtualMachine
enum class vmstatus_t {
	GOOD=0,  
	COMPLETE=1, 
	ERROR, 
	RANDOM_CHOICE,
	RANDOM_CHOICE_NO_DELETE
}; 

