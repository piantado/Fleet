#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Stack.h"

template<typename VirtualMachineState_t>
class Program : public Stack<Instruction<VirtualMachineState_t>> {
public:
	Program() {
		// This is chosen with a little experimentation, designed to prevent us from having to reallocate too often
		// when we linearize, and also save us from having to compute program_size() when we linearize (since thats slow)
		reserve(64);
	}
	
	// Remove n from the stack
	void popn(size_t n) {
		for(size_t i=0;i<n;i++) {
			pop();
		}
	}
};

