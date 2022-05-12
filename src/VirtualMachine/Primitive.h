#pragma once 

#include "Instruction.h"

/**
 * @class Builtin
 * @author Steven Piantadosi
 * @date 13/11/21
 * @file Builtins.h
 * @brief The Builtin type just stores a function pointer and an Op command. 
 * 		  This makes it a bit handier to define Primitives because they all take this
 *        form. 
 */
template<typename T=void, typename... args> 
struct Primitive {
	
	Op op;
	void* f;
	
	Primitive(const Primitive<T,args...>& other) = delete; // probably not the best idea to copy these
	Primitive(const Primitive<T,args...>&& other) = delete; // probably not the best idea to copy these
	
	/**
	 * @brief Constructor here assumes the std::function type
	 * @param o
	 * @param _f
	 */		
	template<typename VirtualMachineState_t>
	Primitive(Op o, typename VirtualMachineState_t::FT* _f) : f((void*)_f), op(o) {
		assert(f != nullptr);
	}	
	
	/**
	 * @brief This constructor takes an ordinary function pointer and converts it
	 * @param o
	 */	
	template<typename VirtualMachineState_t>
	Primitive(Op o, void(*_f)(VirtualMachineState_t*,int)) : op(o) {
		f = (void*)(new typename VirtualMachineState_t::FT(_f)); // make a copy
		assert(f != nullptr);
	}
	
	// this is helpful for other Builtins to call even though it is not used in VMS running
	template<typename VirtualMachineState_t>
	void call(VirtualMachineState_t* vms, int arg) {
		auto myf = reinterpret_cast<typename VirtualMachineState_t::FT*>(f);
		(*myf)(vms, arg);
	}
	
	Instruction makeInstruction(int arg=0) {
		return Instruction(f,arg);
	}
};
