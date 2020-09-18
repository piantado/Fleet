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





class Instruction { 
public:

	// the function type we use takes a virtual machine state and returns a status
	int arg;
	void* f;

	// constructors to make this a little easier to deal with
	Instruction(void* _f, int a=0x0) : f(_f), arg(a) {	
	}
	
	bool operator==(const Instruction& i) const {
		return f==i.f and arg==i.arg;
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
