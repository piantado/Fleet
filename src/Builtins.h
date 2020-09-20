#pragma once 

#include "Errors.h"

// We'll bundle togehter some types and Ops so that
// they can more direclty be added to the grammar and get the op right
// We need this Op so that a rule can know what kind of op it's using, since
// e.g. If and And need special linearization
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
	
	// this is helpful for other Builtins to call 
	template<typename VirtualMachineState_t>
	void call(VirtualMachineState_t* vms, int arg) {
		auto myf = reinterpret_cast<std::function<void(VirtualMachineState_t*,int)>*>(f);
		(*myf)(vms, arg);
	}
	
	Instruction makeInstruction(int arg=0) {
		return Instruction(f,arg);
	}
};

namespace Builtins {
	
	template<typename Grammar_t>
	Builtin<typename Grammar_t::input_t> X(Op::X, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
		assert(!vms->xstack.empty());
		vms->template push<typename Grammar_t::input_t>(vms->xstack.top()); // not a pop!
	});
	
	template<typename Grammar_t>
	Builtin<bool,bool,bool> And(Op::And, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
	
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
	
	template<typename Grammar_t>
	Builtin<bool,bool,bool> Or(Op::Or, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
	
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
	
	template<typename Grammar_t>
	Builtin<bool,bool> Not(Op::Not, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
		vms->push(not vms->template getpop<bool>()); 
	});
	
	
	template<typename Grammar_t, typename T>
	Builtin<T,bool,T,T> If(Op::If, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
		// Op::If has to short circuit and skip (pop some of the stack) 
		bool b = vms->template getpop<bool>(); // bool has already evaluted
		
		// now ops must skip the xbranch
		if(!b) vms->template program.popn(arg);
		else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
	});
	
	template<typename Grammar_t>
	Builtin<> Jmp(Op::Jmp, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
		vms->program.popn(arg);
	});
	
	template<typename Grammar_t>
	Builtin<> PopX(Op::PopX, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
		vms->xstack.pop();
	});	
	
	template<typename Grammar_t>
	Builtin<typename Grammar_t::output_t, typename Grammar_t::input_t> 
		Recurse(Op::Recurse, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
			
			using input_t = Grammar_t::VirtualMachineState_t::input_t;
			
			assert(vms->program_loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				vms->status = vmstatus_t::RECURSION_DEPTH;
				return;
			}

			// if we get here, then we have processed our arguments and they are stored in the input_t stack. 
			// so we must move them to the x stack (where there are accessible by op_X)
			auto mynewx = vms->template getpop<input_t>();
			vms->xstack.push(std::move(mynewx));
			vms->program.push(Builtins::PopX<Grammar_t>.makeInstruction()); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
			
			// push this program 
			// but we give i.arg so that we can pass factorized recursed
			// in argument if we want to
			vms->program_loader->push_program(vms->program,arg); 
			
			// after execution is done, the result will be pushed onto output_t
			// which is what gets returned when we are all done
			
	});
	
	
	template<typename Grammar_t>
	Builtin<bool> Flip(Op::Flip, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
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
	
	
	template<typename Grammar_t>
	Builtin<bool> FlipP(Op::FlipP, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {
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
	
	
	
	template<typename Grammar_t>
	Builtin<> Mem(Op::Mem, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {	
		auto memindex = vms->memstack.top(); vms->memstack.pop();
		if(vms->mem.count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrte if you have
			vms->mem[memindex] = vms->template gettop<typename Grammar_t::output_t>(); // what I should memoize should be on top here, but don't remove because we also return it
		}
	});
	
	
	template<typename Grammar_t>
	Builtin<> SafeRecurse(Op::SafeRecurse, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {	
		using input_t = Grammar_t::VirtualMachineState_t::input_t;
		using output_t = Grammar_t::VirtualMachineState_t::output_t;
		
		assert(not vms->template stack<input_t>().empty());
		
		// if the size of the top is zero, we return output{}
		if(vms->template stack<input_t>().top().size() == 0) { 
			vms->template getpop<input_t>(); // this would have been the argument
			vms->template push<output_t>(output_t{}); //push default (null) return
		}
		else {
			Recurse<Grammar_t>.call(vms,arg);
		}
	});
	
	
	template<typename Grammar_t>
	Builtin<> MemRecurse(Op::MemRecurse, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {	
		assert(vms->program_loader != nullptr);
						
		if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
			vms->status = vmstatus_t::RECURSION_DEPTH;
			return;
		}
				
		auto x = vms->template getpop<Grammar_t::input_t>(); // get the argumen
		auto memindex = std::make_pair(arg,x);
		
		if(vms->mem.count(memindex)){
			vms->push(std::move(vms->mem[memindex])); 
		}
		else {	
			vms->xstack.push(x);	
			vms->memstack.push(memindex); // popped off by op_MEM
			vms->program.push(Builtins::Mem<Grammar_t>.makeInstruction());
			vms->program.push(Builtins::PopX<Grammar_t>.makeInstruction());
			vms->program_loader->push_program(vms->program,arg); // this leaves the answer on top
		}		
	});


	template<typename Grammar_t>
	Builtin<> SafeMemRecurse(Op::SafeMemRecurse, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {	
		
		using input_t = Grammar_t::VirtualMachineState_t::input_t;
		using output_t = Grammar_t::VirtualMachineState_t::output_t;
		
		assert(not vms->template stack<input_t>().empty());
		
		// if the size of the top is zero, we return output{}
		if(vms->template stack<input_t>().top().size() == 0) { 
			vms->template getpop<input_t>(); // this would have been the argument
			vms->template push<output_t>(output_t{}); //push default (null) return
		}
		else {
			MemRecurse<Grammar_t>.call(vms,arg);
		}
	});
	
	template<typename Grammar_t>
	Builtin<> NoOp(Op::NoOp, +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void {	
		
	});

}

