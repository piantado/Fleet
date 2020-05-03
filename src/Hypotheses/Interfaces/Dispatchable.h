#pragma once 

/**
 * @class Dispatchable
 * @author steven piantadosi
 * @date 03/02/20
 * @file Dispatchable.h
 * @brief A class is dispatchable if it is able to implement custom operations and put its program onto a Program
 */
template<typename t_input, typename t_output>
class Dispatchable {
public:
	
	// This loads a program into the stack. Short is passed here in case we have a factorized lexicon,
	// which for now is a pretty inelegant hack. 
	virtual void push_program(Program&, short)=0;
};

