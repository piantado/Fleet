#pragma once 

#include "Instruction.h"
#include "Stack.h"

template<typename VirtualMachineState_t> class Program;

/**
 * @class ProgramLoader
 * @author Steven Piantadosi
 * @date 03/09/20
 * @file Callable.h
 * @brief This tiny class is just the class of things that can be loaders -- it allows you to write a program.
 * 		  And importantly it doesn't have any tempalte args, which simplifies a lot
 */
template<typename VirtualMachineState_t>
class ProgramLoader {
public:


	// This is a bit of hack -- we need a line here for eahc kind of key we might want to a lexicon. 
	// For reasons I don't fully understand, the linker will not find this if templated. 
	
	virtual void push_program(Program<VirtualMachineState_t>& s)                      { throw NotImplementedError(); }
	virtual void push_program(Program<VirtualMachineState_t>& s, const short a)       { throw NotImplementedError(); }
	virtual void push_program(Program<VirtualMachineState_t>& s, const int a)         { throw NotImplementedError(); }
	virtual void push_program(Program<VirtualMachineState_t>& s, const std::string k) { throw NotImplementedError(); }
	
};


/**
 * @class Program
 * @author Steven Piantadosi
 * @date 03/09/21
 * @file Program.h
 * @brief A program here stores just a stack of instructions which can be executed by the VirtualMachineState_t.
 * 		  We've optimized the reserve in the constructor to be the fasest. 
 */
template<typename VirtualMachineState_t>
class Program : public Stack<Instruction> {
public:

	ProgramLoader<VirtualMachineState_t>* loader; 

	Program(ProgramLoader<VirtualMachineState_t>* pl=nullptr) : loader(pl) {
		// This is chosen with a little experimentation, designed to prevent us from having to reallocate too often
		// when we linearize, and also save us from having to compute program_size() when we linearize (since thats slow)
		this->reserve(64);
	}	
};

