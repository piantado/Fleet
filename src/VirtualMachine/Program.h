#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Stack.h"

class Program : public Stack<Instruction> {
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



/**
 * @class ProgramLoader
 * @author steven piantadosi
 * @date 03/02/20
 * @file Dispatchable.h
 * @brief A class is a program loader if it can push stuff onto a program. 
 */
class ProgramLoader {
public:
	
	// This loads a program into the stack. Short is passed here in case we have a factorized lexicon,
	// which for now is a pretty inelegant hack. 
	virtual void push_program(Program&, short)=0;
	virtual size_t program_size(short) = 0;
};

