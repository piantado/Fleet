#pragma once

#include <assert.h>
#include <iostream>
#include <variant>

#include "VMSRuntimeError.h"
#include "Ops.h"
#include "VMStatus.h"
 
struct Instruction { 
public:

	// the function type we use takes a virtual machine state and returns a status
	void* f;
	int arg;
	
	// constructors to make this a little easier to deal with
	Instruction(void* _f, int a=0x0) : f(_f), arg(a) {	
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
