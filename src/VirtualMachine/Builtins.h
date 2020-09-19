#pragma once 


#include "Errors.h"

// We'll bundle togehter some types and Ops so that
// they can more direclty be added to the grammar and get the op right
template<typename T=void, typename... args> 
struct Builtin {
	Op op;
	void* f;
	
	template<typename VirtualMachineState_t>
	Builtin(Op o, std::function<void(VirtualMachineState_t*,int)>* _f) : f((void*)_f), op(o) {
	}	
	
	template<typename VirtualMachineState_t>
	Builtin(Op o, void(*_f)(VirtualMachineState_t*,int)) : op(o) {
		f = (void*)(new std::function<void(VirtualMachineState_t*,int)>(_f)); // make a copy
	}
	
	Instruction makeInstruction(int arg=0) {
		return Instruction(f,arg);
	}
};

namespace Builtins {
	
	template<typename T, typename VirtualMachineState_t>
	Builtin<T> X(Op::X, +[](VirtualMachineState_t* vms, int arg) -> void {
		assert(!vms->xstack.empty());
		vms->template push<T>(vms->xstack.top()); // not a pop!
	});
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool,bool> And(Op::And, +[](VirtualMachineState_t* vms, int arg) -> void {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluted
			
		if(!b) {
			vms->program.popn(arg); // pop off the other branch 
			vms->template push<bool>(false); //  entire and must be false
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	});
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool,bool> Or(Op::Or, +[](VirtualMachineState_t* vms, int arg) -> void {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluted
			
		if(b) {
			vms->program.popn(arg); // pop off the other branch 
			vms->template push<bool>(true);
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	});
	
	template<typename VirtualMachineState_t>
	Builtin<bool,bool> Not(Op::Not, +[](VirtualMachineState_t* vms, int arg) -> void {
		vms->push(not vms->template getpop<bool>()); 
	});
	
	
	template<typename T, typename VirtualMachineState_t>
	Builtin<T,bool,T,T> If(Op::If, +[](VirtualMachineState_t* vms, int arg) -> void {
		// Op::If has to short circuit and skip (pop some of the stack) 
		bool b = vms->template getpop<bool>(); // bool has already evaluted
		
		// now ops must skip the xbranch
		if(!b) vms->template program.popn(arg);
		else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
	});
	
	template<typename VirtualMachineState_t>
	Builtin<> Jmp(Op::Jmp, +[](VirtualMachineState_t* vms, int arg) -> void {
		vms->program.popn(arg);
	});
	
	template<typename VirtualMachineState_t>
	Builtin<> PopX(Op::PopX, +[](VirtualMachineState_t* vms, int arg) -> void {
		vms->xstack.pop();
	});	
	
	template<typename VirtualMachineState_t>
	Builtin<typename VirtualMachineState_t::output_t, typename VirtualMachineState_t::input_t> 
		Recurse(Op::Recurse, +[](VirtualMachineState_t* vms, int arg) -> void {
			assert(vms->program_loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				vms->status = vmstatus_t::RECURSION_DEPTH;
				return;
			}





			// TODO:
			
			
			
			// Add this back in:

//			else if( vms->program_loader->program_size(arg) + vms->program.size() > vms->remaining_steps()) { // check if we have too many
//				vms->status = vmstatus_t::RUN_TOO_LONG;
//				return;
//			}
			
			// if we get here, then we have processed our arguments and they are stored in the input_t stack. 
			// so we must move them to the x stack (where there are accessible by op_X)
			auto mynewx = vms->template getpop<typename VirtualMachineState_t::input_t>();
			vms->xstack.push(std::move(mynewx));
			vms->program.push(Builtins::PopX<VirtualMachineState_t>.makeInstruction()); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
			
			// push this program 
			// but we give i.arg so that we can pass factorized recursed
			// in argument if we want to
			vms->program_loader->push_program(vms->program,arg); 
			
			// after execution is done, the result will be pushed onto output_t
			// which is what gets returned when we are all done
			
	});
	
	
	template<typename VirtualMachineState_t>
	Builtin<bool> Flip(Op::Flip, +[](VirtualMachineState_t* vms, int arg) -> void {
		assert(vms->pool != nullptr);
		
		// push both routes onto the stack
		vms->pool->copy_increment_push(vms, true, -LOG2);
		bool b = vms->pool->increment_push(vms, false, -LOG2); 
		
		// TODO: This is clumsy, ugly mechanism -- need to re-do
		
		// since we pushed this back onto the queue (via increment_push), we need to tell the 
		// pool not to delete this, so we send back this special signal
		if(b) { // theoutcome of increment_push decides whether I am deleted or not
			vms->status = vmstatus_t::RANDOM_CHOICE_NO_DELETE; 
		}
		else {
			vms->status = vmstatus_t::RANDOM_CHOICE; 
		}
	});
	
	
	template<typename VirtualMachineState_t>
	Builtin<bool> FlipP(Op::FlipP, +[](VirtualMachineState_t* vms, int arg) -> void {
		assert(vms->pool != nullptr);
		
		// get the coin weight
		double p = vms->template getpop<double>(); 
		
		// some checking
		if(std::isnan(p)) { p = 0.0; } // treat nans as 0s
		assert(p <= 1.0 and p >= 0.0);
		
		// push both routes onto the stack
		vms->pool->copy_increment_push(vms, true, log(p));
		bool b = vms->pool->increment_push(vms, false, log(1.0-p)); 
		
		// TODO: This is clumsy, ugly mechanism -- need to re-do
		
		// since we pushed this back onto the queue (via increment_push), we need to tell the 
		// pool not to delete this, so we send back this special signal
		if(b) { // theoutcome of increment_push decides whether I am deleted or not
			vms->status = vmstatus_t::RANDOM_CHOICE_NO_DELETE; 
		}
		else {
			vms->status = vmstatus_t::RANDOM_CHOICE; 
		}
	});
	
	
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

