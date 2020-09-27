#pragma once 

/**
 * @class ProgramLoader
 * @author Steven Piantadosi
 * @date 03/09/20
 * @file Callable.h
 * @brief This tiny class is just the class of things that can be loaders -- it allows you to write a program.
 * 		  Adn importantly it doesn't have any tempalte args, which simplifies a lot
 */
 
class ProgramLoader {
public:
	// Ways to push a program onto s
	[[nodiscard]] virtual size_t program_size(short s) = 0;
	
	virtual void push_program(Program& s, short k=0) = 0;

};
