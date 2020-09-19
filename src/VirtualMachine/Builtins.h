#pragma once 


#include "Errors.h"

// We'll bundle togehter some types and Ops so that
// they can more direclty be added to the grammar and get the op right
template<typename T, typename... args> 
struct Builtin {
	Op op;
	void* f;
	
	template<typename VirtualMachineState_t>
	Builtin(std::function<void(VirtualMachineState_t*,int)>* _f, Op o = Op::Standard) : f((void*)_f), op(o) {
	}	
	
	template<typename VirtualMachineState_t>
	Builtin(void(*_f)(VirtualMachineState_t*,int), Op o = Op::Standard) : op(o) {
		f = (void*)(new std::function<void(VirtualMachineState_t*,int)>(_f)); // make a copy
	}
};

namespace Builtins {
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool,bool> And( +[](VirtualMachineState_t* vms, int arg) -> void {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluted
			
		if(!b) {
			vms->opstack.popn(arg); // pop off the other branch 
			vms->template push<bool>(false); //  entire and must be false
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	}, Op::And);
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool,bool> Or( +[](VirtualMachineState_t* vms, int arg) -> void {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluted
			
		if(b) {
			vms->opstack.popn(arg); // pop off the other branch 
			vms->template push<bool>(true);
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	}, Op::Or);
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool> Not( +[](VirtualMachineState_t* vms, int arg) -> void {
		vms->push(not vms->template getpop<bool>()); 
	}, Op::Not);
	
//
//	// these are short-circuit versions of And and Or
//	struct And : public BuiltinPrimitive<bool,bool,bool> {
//		And(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool,bool,bool>{fmt, BuiltinOp::op_AND, _p} { }		
//	};
//	struct Or : public BuiltinPrimitive<bool,bool,bool> {
//		Or(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool,bool,bool>{fmt, BuiltinOp::op_OR, _p} { }		
//	};
//	struct Not : public BuiltinPrimitive<bool,bool> {
//		Not(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool,bool>{fmt, BuiltinOp::op_NOT, _p} { }		
//	};
//	
//	// If is fancy because it must short circuit depending on the outcome of the bool
//	template<typename t> struct If : public BuiltinPrimitive<t,bool,t,t> {
//		If(std::string fmt, double _p=1.0) : BuiltinPrimitive<t,bool,t,t>{fmt, BuiltinOp::op_IF, _p} { }		
//	};
//	
//	template<typename t> struct X : public BuiltinPrimitive<t> {
//		X(std::string fmt, double _p=1.0) : BuiltinPrimitive<t>{fmt, BuiltinOp::op_X, _p} { }		
//	};
//	
//	struct Flip : public BuiltinPrimitive<bool> {
//		Flip(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool>{fmt, BuiltinOp::op_FLIP, _p} { }		
//	};
//	
//	struct FlipP : public BuiltinPrimitive<bool, double> {
//		FlipP(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool, double>{fmt, BuiltinOp::op_FLIPP, _p} { }		
//	};
//	
//	template<typename t_out, typename t_in> struct Recurse : public BuiltinPrimitive<t_out, t_in> {
//		Recurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_RECURSE, _p} { }		
//	};
//	
//	template<typename t_out, typename t_in> struct SafeRecurse : public BuiltinPrimitive<t_out, t_in> {
//		SafeRecurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_SAFE_RECURSE, _p} { }		
//	};
//
//	template<typename t_out, typename t_in> struct SafeMemRecurse : public BuiltinPrimitive<t_out, t_in> {
//		SafeMemRecurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_SAFE_MEM_RECURSE, _p} { }
//	};
}

