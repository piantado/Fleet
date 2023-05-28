#pragma once 

#include "Numerics.h"
#include "IO.h"
#include "Errors.h"
#include "Primitive.h"

// Define this because it's an ugly expression to keep typing and we might want to change all at once
#define BUILTIN_LAMBDA             +[](typename Grammar_t::VirtualMachineState_t* vms, int arg) -> void

namespace Builtins {
		
	
	template<typename Grammar_t>
	Primitive<typename Grammar_t::input_t> X(Op::X, BUILTIN_LAMBDA {
		assert(!vms->xstack.empty());
		vms->template push<typename Grammar_t::input_t>(vms->xstack.top()); // not a pop!
	});
	
	template<typename Grammar_t>
	Primitive<bool,bool,bool> And(Op::And, BUILTIN_LAMBDA {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluated
			
		if(!b) {
			vms->program.popn(arg); // pop off the other branch 
			vms->template push<bool>(false); //  entire and must be false
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	});
	
	template<typename Grammar_t>
	Primitive<bool,bool,bool> Or(Op::Or, BUILTIN_LAMBDA {
	
		// process the short circuit
		bool b = vms->template getpop<bool>(); // bool has already evaluated
			
		if(b) {
			vms->program.popn(arg); // pop off the other branch 
			vms->template push<bool>(true);
		}
		else {
			// else our value is just the other value -- true when its true and false when its false
		}		
	});
	
	template<typename Grammar_t>
	Primitive<bool,bool> Not(Op::Not, BUILTIN_LAMBDA {
		vms->push(not vms->template getpop<bool>()); 
	});
	
	
	template<typename Grammar_t>
	Primitive<bool,bool,bool> Implies(Op::Implies, BUILTIN_LAMBDA {
		bool x = vms->template getpop<bool>(); 
		bool y = vms->template getpop<bool>(); 
		vms->template push<bool>( (not x) or y);
	});
	
		
	template<typename Grammar_t>
	Primitive<bool,bool,bool> Iff(Op::Iff, BUILTIN_LAMBDA {
		bool x = vms->template getpop<bool>(); 
		bool y = vms->template getpop<bool>(); 
		vms->template push<bool>( x==y );
	});
	
	
	template<typename Grammar_t, typename T>
	Primitive<T,bool,T,T> If(Op::If, BUILTIN_LAMBDA {
		// Op::If has to short circuit and skip (pop some of the stack) 
		bool b = vms->template getpop<bool>(); // bool has already evaluted
		
		// now ops must skip the xbranch
		if(!b) vms->program.popn(arg);
		else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
	});
	
	template<typename Grammar_t>
	Primitive<> Jmp(Op::Jmp, BUILTIN_LAMBDA {
		vms->program.popn(arg);
	});
	
	template<typename Grammar_t>
	Primitive<> PopX(Op::PopX, BUILTIN_LAMBDA {
		vms->xstack.pop();
	});	
	
	template<typename Grammar_t>
	Primitive<bool> Flip(Op::Flip, BUILTIN_LAMBDA {
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
	Primitive<bool,double> FlipP(Op::FlipP, BUILTIN_LAMBDA {
		assert(vms->pool != nullptr);
		
		// get the coin weight
		double p = vms->template getpop<double>(); 
		
		// some checking
		if(std::isnan(p)) { p = 0.0; } // treat nans as 0s
		if(p > 1.0 or p < 0.0) {
			print("*** Error, received p not in [0,1]:", p);
			assert(false);
		}
		
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
	Primitive<bool,double> SafeFlipP(Op::SafeFlipP, BUILTIN_LAMBDA {
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
	
	template<typename Grammar_t, typename t, typename T=std::set<t>>
	Primitive<t, T> Sample(Op::Sample, BUILTIN_LAMBDA {
				
		// implement sampling from the set.
		// to do this, we read the set and then push all the alternatives onto the stack
		auto s = vms->template getpop<T>();
			
		// One useful optimization here is that sometimes that set only has one element. So first we check that, and if so we don't need to do anything 
		// also this is especially convenient because we only have one element to pop
		if(s.size() == 1) {
			auto v = std::move(*s.begin());
			vms->template push<t>(std::move(v));
		}
		else {
			// else there is more than one, so we have to copy the stack and increment the lp etc for each
			// NOTE: The function could just be this latter branch, but that's much slower because it copies vms
			// even for single stack elements
			
			// now just push on each, along with their probability
			// which is here decided to be uniform.
			const double lp = -log(s.size());
			for(const auto& x : s) {
				bool b = vms->pool->copy_increment_push(vms,x,lp);
				if(not b) break; // we can break since all of these have the same lp -- if we don't add one, we won't add any!
			}
			
			vms->status = vmstatus_t::RANDOM_CHOICE; // we don't continue with this context		
		}	
	});
	
	template<typename T, typename Grammar_t, size_t MX>
	Primitive<T, T> Sample_int(Op::Sample, BUILTIN_LAMBDA {
		// sample 0,1,2,3, ... <first argument>-1
		
		const auto mx = vms->template getpop<T>();
		if(mx >= MX) throw VMSRuntimeError(); // MX stops us from having stupidly high values
		
		const double lp = -log(mx);
		for(T i=0;i<mx;i++) { 
			bool b = vms->pool->copy_increment_push(vms,i,lp);
			if(not b) break; // we can break since all of these have the same lp -- if we don't add one, we won't add any!
		}
		vms->status = vmstatus_t::RANDOM_CHOICE; // we don't continue with this context		
	});
	
	
		
	template<typename T, typename Grammar_t, size_t MX>
	Primitive<T, T, double> Sample_int_geom(Op::Sample, BUILTIN_LAMBDA {
		// sample 0,1,2,3, ... <first argument>-1
		
		const auto mx = vms->template getpop<T>();
		if(mx >= MX) throw VMSRuntimeError(); // MX stops us from having stupidly high values
		const auto p = vms->template getpop<double>();
		
		// find the normalizing constant (NOTE: not in log space for now)
		double z = 0.0;
		for(size_t i=0;i<mx;i++) 
			z += pow(p,i)*(1-p);
		const double lz = log(z);
		
		for(T i=0;i<mx;i++) { 
			const double lp = i * log(p) + log(1-p) - lz;
			bool b = vms->pool->copy_increment_push(vms,i,lp);
			if(not b) break; // we can break since all of these have the same lp -- if we don't add one, we won't add any!
		}
		vms->status = vmstatus_t::RANDOM_CHOICE; // we don't continue with this context		
	});
	
	template<typename Grammar_t, typename key_t, typename output_t=typename Grammar_t::output_t>
	Primitive<> Mem(Op::Mem, BUILTIN_LAMBDA {	
		auto memindex = vms->template memstack<key_t>().top(); vms->template memstack<key_t>().pop();
		if(vms->template mem<key_t>().count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrite if you have
			vms->template mem<key_t>()[memindex] = vms->template gettop<output_t>(); // what I should memoize should be on top here, but don't remove because we also return it
		}
	});
	
		
	template<typename Grammar_t>
	Primitive<> NoOp(Op::NoOp, BUILTIN_LAMBDA {	
	});

	template<typename Grammar_t>
	Primitive<> UnusedNoOp(Op::NoOp, BUILTIN_LAMBDA {	
		assert(false);
	});
	
	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Primitive<output_t,input_t> Recurse(Op::Recurse, BUILTIN_LAMBDA {
			
			assert(vms->program.loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				throw VMSRuntimeError();
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
	Primitive<output_t, input_t> SafeRecurse(Op::SafeRecurse, BUILTIN_LAMBDA {	
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
	Primitive<output_t,input_t>  MemRecurse(Op::MemRecurse, BUILTIN_LAMBDA {	 // note the order switch -- that's right!
		assert(vms->program.loader != nullptr);
		
		using mykey_t = short; // this is just the default type used for non-lex recursion
		
		if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
			throw VMSRuntimeError();
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
    Primitive<output_t,input_t> SafeMemRecurse(Op::SafeMemRecurse, BUILTIN_LAMBDA {	
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
	
	/**
	 * @brief This is a kind of recursion that, when it reaches the max recursion depth, returns empty string. This allows
	 * 		  us to (approximately) deal with infinite sequences. 
	 * @return 
	 */
	template<typename Grammar_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Primitive<output_t,input_t> RecurseEmptyOnDepthException(Op::Recurse, BUILTIN_LAMBDA {
			
			assert(vms->program.loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				auto mynewx = vms->template getpop<input_t>();
				vms->template push<output_t>(output_t{});
			}
			else {

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
			}
	});

	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
			
	template<typename Grammar_t,
		     typename key_t,
			 typename input_t=typename Grammar_t::input_t,
			 typename output_t=typename Grammar_t::output_t>
	Primitive<output_t,key_t,input_t> LexiconRecurse(Op::LexiconRecurse, BUILTIN_LAMBDA {
			
			assert(vms->program.loader != nullptr);
							
			if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
				throw VMSRuntimeError();
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
	Primitive<output_t,key_t,input_t> LexiconSafeRecurse(Op::LexiconSafeRecurse, BUILTIN_LAMBDA {
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
	Primitive<output_t,key_t,input_t> LexiconMemRecurse(Op::LexiconMemRecurse, BUILTIN_LAMBDA {
		assert(vms->program.loader != nullptr);
						
		if(vms->recursion_depth++ > vms->MAX_RECURSE) { // there is one of these for each recurse
			throw VMSRuntimeError();
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
	Primitive<output_t,key_t,input_t> LexiconSafeMemRecurse(Op::LexiconSafeMemRecurse, BUILTIN_LAMBDA {
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

