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
	Builtin(Op o, typename VirtualMachineState_t::FT* _f) : f((void*)_f), op(o) {
	}	
	
	template<typename VirtualMachineState_t>
	Builtin(Op o, void(*_f)(VirtualMachineState_t*,int)) : op(o) {
		f = (void*)(new typename VirtualMachineState_t::FT(_f)); // make a copy
	}
	
	// this is helpful for other Builtins to call 
	template<typename VirtualMachineState_t>
	void call(VirtualMachineState_t* vms, int arg) {
		auto myf = reinterpret_cast<typename VirtualMachineState_t::FT*>(f);
		(*myf)(vms, arg);
	}
	
	Instruction makeInstruction(int arg=0) {
		return Instruction(f,arg);
	}
};

namespace Builtins {
	
	// Define this because it's an ugly expression to keep typing and we might want to change all at once
	#define BUILTIN_LAMBDA             +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void
	
	template<typename Grammar_t>
	Builtin<typename Grammar_t::input_t> X(Op::X, BUILTIN_LAMBDA {
		assert(!vms->xstack.empty());
		vms->template push<typename Grammar_t::input_t>(vms->xstack.top()); // not a pop!
	});
	
	template<typename Grammar_t>
	Builtin<bool,bool,bool> And(Op::And, BUILTIN_LAMBDA {
	
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
	Builtin<bool,bool,bool> Or(Op::Or, BUILTIN_LAMBDA {
	
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
	Builtin<bool,bool> Not(Op::Not, BUILTIN_LAMBDA {
		vms->push(not vms->template getpop<bool>()); 
	});
	
	
	template<typename Grammar_t, typename T>
	Builtin<T,bool,T,T> If(Op::If, BUILTIN_LAMBDA {
		// Op::If has to short circuit and skip (pop some of the stack) 
		bool b = vms->template getpop<bool>(); // bool has already evaluted
		
		// now ops must skip the xbranch
		if(!b) vms->program.popn(arg);
		else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
	});
	
	template<typename Grammar_t>
	Builtin<> Jmp(Op::Jmp, BUILTIN_LAMBDA {
		vms->program.popn(arg);
	});
	
	template<typename Grammar_t>
	Builtin<> PopX(Op::PopX, BUILTIN_LAMBDA {
		vms->xstack.pop();
	});	
	
	template<typename Grammar_t>
	Builtin<bool> Flip(Op::Flip, BUILTIN_LAMBDA {
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
	Builtin<bool,double> FlipP(Op::FlipP, BUILTIN_LAMBDA {
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
	
	
	// This is a version of FlipP that doesn't complain if p>1 or p<0 -- it
	// just sets them to those values
	template<typename Grammar_t>
	Builtin<bool,double> SafeFlipP(Op::SafeFlipP, BUILTIN_LAMBDA {
		assert(vms->pool != nullptr);
		
		// get the coin weight
		double p = vms->template getpop<double>(); 
		
		// some checking
		if(std::isnan(p))   p = 0.0;  // treat nans as 0s
		else if(p > 1.0)    p = 1.0;
		else if(p < 0.0)    p = 0.0;
		
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
	
	
	template<typename Grammar_t, typename key_t, typename output_t=typename Grammar_t::output_t>
	Builtin<> Mem(Op::Mem, BUILTIN_LAMBDA {	
		auto memindex = vms->template memstack<key_t>().top(); vms->template memstack<key_t>().pop();
		if(vms->template mem<key_t>().count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrite if you have
			vms->template mem<key_t>()[memindex] = vms->template gettop<output_t>(); // what I should memoize should be on top here, but don't remove because we also return it
		}
	});
	
		
	template<typename Grammar_t>
	Builtin<> NoOp(Op::NoOp, BUILTIN_LAMBDA {	
		
	});

	
	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,input_t> Recurse(Op::Recurse, BUILTIN_LAMBDA {
			
			assert(vms->program.loader != nullptr);
							
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
			vms->program.loader->push_program(vms->program); 
			
			// after execution is done, the result will be pushed onto output_t
			// which is what gets returned when we are all done
			
	});
	
	
	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t, input_t> SafeRecurse(Op::SafeRecurse, BUILTIN_LAMBDA {	
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
	
	

	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,input_t>  MemRecurse(Op::MemRecurse, BUILTIN_LAMBDA {	 // note the order switch -- that's right!
		assert(vms->program.loader != nullptr);
		
		using mykey_t = short; // this is just the default type used for non-lex recursion
		
		if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
			vms->status = vmstatus_t::RECURSION_DEPTH;
			return;
		}
				
		auto x = vms->template getpop<input_t>(); // get the argument
		auto memindex = std::make_pair(arg,x);
		
		if(vms->template mem<mykey_t>().count(memindex)){
			vms->push(vms->template mem<mykey_t>()[memindex]); // hmm probably should not be a move?
		}
		else {	
			vms->xstack.push(x);	
			vms->program.push(Builtins::PopX<Grammar_t>.makeInstruction());

			vms->template memstack<mykey_t>().push(memindex); // popped off by op_MEM			
			vms->program.push(Builtins::Mem<Grammar_t,mykey_t,output_t>.makeInstruction());

			vms->program.loader->push_program(vms->program); // this leaves the answer on top
		}		
	});
	

	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
    Builtin<output_t,input_t> SafeMemRecurse(Op::SafeMemRecurse, BUILTIN_LAMBDA {	
		assert(not vms->template stack<input_t>().empty());
		
		// if the size of the top is zero, we return output{}
		if(vms->template stack<input_t>().top().size() == 0) { 
			vms->template getpop<input_t>(); // this would have been the argument
			vms->template push<output_t>(output_t{}); //push default (null) return
		}
		else {
			MemRecurse<Grammar_t>.call(vms, arg);
		}
	});
	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
			
	template<typename Grammar_t,
		     typename key_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,key_t,input_t> LexiconRecurse(Op::LexiconRecurse, BUILTIN_LAMBDA {
			
			assert(vms->program.loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				vms->status = vmstatus_t::RECURSION_DEPTH;
				return;
			}
			
			// the key here is the index into the lexicon
			// NOTE: this is popped before the argument (which is relevant when they are the same type)
			auto key = vms->template getpop<key_t>();

			// if we get here, then we have processed our arguments and they are stored in the input_t stack. 
			// so we must move them to the x stack (where there are accessible by op_X)
			auto mynewx = vms->template getpop<input_t>();
			vms->xstack.push(std::move(mynewx));
			vms->program.push(Builtins::PopX<Grammar_t>.makeInstruction()); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
			

			
			// push this program 
			// but we give i.arg so that we can pass factorized recursed
			// in argument if we want to
			vms->program.loader->push_program(vms->program,key); 
	});
	
	
	template<typename Grammar_t,
		     typename key_t,
		     typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,key_t,input_t> LexiconSafeRecurse(Op::LexiconSafeRecurse, BUILTIN_LAMBDA {
			assert(not vms->template stack<input_t>().empty());
			assert(vms->program.loader != nullptr);
				
			// if the size of the top is zero, we return output{}
			if(vms->template stack<input_t>().top().size() == 0) { 
				vms->template getpop<key_t>();
				vms->template getpop<input_t>(); // this would have been the argument
							
				vms->template push<output_t>(output_t{}); //push default (null) return
			}
			else {
				LexiconRecurse<Grammar_t,key_t>.call(vms,arg); // NOTE: Key is popped in this call
			}
		
	});
	
	
	template<typename Grammar_t,
		     typename key_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,key_t,input_t> LexiconMemRecurse(Op::LexiconMemRecurse, BUILTIN_LAMBDA {
		assert(vms->program.loader != nullptr);
						
		if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
			vms->status = vmstatus_t::RECURSION_DEPTH;
			return;
		}
		
		auto key = vms->template getpop<key_t>();
		auto x = vms->template getpop<input_t>(); // get the argument
		
		auto memindex = std::make_pair(key,x);
		
		if(vms->template mem<key_t>().count(memindex)){
			vms->push(vms->template mem<key_t>()[memindex]); // copy over here
		}
		else {	
			vms->xstack.push(x);	
			vms->program.push(Builtins::PopX<Grammar_t>.makeInstruction());

			vms->template memstack<key_t>().push(memindex); // popped off by op_MEM			
			vms->program.push(Builtins::Mem<Grammar_t,key_t,output_t>.makeInstruction());

			vms->program.loader->push_program(vms->program,key);  // this leaves the answer on top
		}				
	});
	

	template<typename Grammar_t,
		     typename key_t,
		     typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Builtin<output_t,key_t,input_t> LexiconSafeMemRecurse(Op::LexiconSafeMemRecurse, BUILTIN_LAMBDA {
			assert(not vms->template stack<input_t>().empty());
			assert(vms->program.loader != nullptr);
				
			// if the size of the top is zero, we return output{}
			if(vms->template stack<input_t>().top().size() == 0) { 
				
				vms->template getpop<key_t>();
				vms->template getpop<input_t>(); // this would have been the argument
							
				vms->template push<output_t>(output_t{}); //push default (null) return
			}
			else {
				LexiconMemRecurse<Grammar_t,key_t>.call(vms,arg);
			}
		
	});
}

