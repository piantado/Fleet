#pragma once

#include <assert.h>
#include <iostream>
#include <variant>

#include "VMSRuntimeError.h"
#include "Ops.h"
#include "VMStatus.h"

// Instead of lambdas, which are hard to put on the stack, 
// we define these callable objects
//template<typename VirtualMachineState_t>
//class VMSFunction {
//	using F = vmstatus_t(*)(VirtualMachineState_t*);
//
//	template<typename T, typename... args> 
//	VMSFunction( T(*fargs)(args...) ) {
//		
//	}
//
//	vmstatus_t operator()(VirtualMachineState_t*) {
//		
//	}
//
//};



/**
* @class Instruction
* @author piantado
* @date 29/01/20
* @file Instruction.h
* @brief  This is how we store an instruction in the program. It can take one of three types:
	 BuiltinOp -- these are operations that are defined as part of Fleet's core library,
					and are implemented automatically in VirtualMachineState::run
	 PrimitiveOp -- these are defined *automatically* through the global variable PRIMITIVES
					  (see Models/RationalRules). When you construct a grammar with these, it automatically
					  figures out all the types and automatically gives each a sequential numbering, which
					  it takes some template magic to access at runtime
	 For any op type, an Instruction always takes an "arg" type that essentially allow us to define classes of instructions
	 for instance, jump takes an arg, factorized recursion uses arg for index, in FormalLanguageTheory
	 we use arg to store which alphabet terminal, etc. 

*/
class Instruction { 
public:

	// the function type we use takes a virtual machine state and returns a status
	//

	void* f;

	// constructors to make this a little easier to deal with
	Instruction(void* _f) : f(_f) {	}

	
	bool operator==(const Instruction& i) const {
		return f==i.f;
	}
		
};

std::ostream& operator<<(std::ostream& stream, Instruction& i) {
	/**
	 * @brief Output for instructions. 
	 * @param stream
	 * @param i
	 * @return 
	 */
	stream << "[INSTR " << i.f << "]";
	return stream;
}
