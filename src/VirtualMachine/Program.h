#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Stack.h"

typedef Stack<Instruction> Program;

// Remove n from the stack
void popn(Program& s, size_t n) {
	for(size_t i=0;i<n;i++) s.pop();
}

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
};

