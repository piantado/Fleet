#pragma once

#include <assert.h>
#include <iostream>
#include <variant>

#include "VMSRuntimeError.h"
#include "Ops.h"
#include "VMStatus.h"
 
/**
* @class Instruction
* @author Steven Piantadosi
* @date 20/09/20
* @file Instruction.h
* @brief f here is a point to a void(VirtualMachineState_t* vms, int arg), where arg
* 	 is just a supplemental argument, used to pass indices in lexica and jump sizes etc
*      for other primitives
*/ 
template<typename VirtualMachineState_t>
struct Instruction { 
public:

	// the function type we use takes a virtual machine state and returns a status
	VirtualMachineState_t::FT* f;
	int arg;
	
	// constructors to make this a little easier to deal with
	Instruction(VirtualMachineState_t::FT* _f=nullptr, int a=0x0) : f(_f), arg(a) {	
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
