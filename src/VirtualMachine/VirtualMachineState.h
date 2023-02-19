#pragma once

#include <type_traits>
#include <map>
#include <utility>
#include <tuple>

#include "Errors.h"
#include "Program.h"
#include "Stack.h"
#include "Statistics/FleetStatistics.h"
#include "RuntimeCounter.h"
#include "VirtualMachineControl.h"

#include "VMSRuntimeError.h"

// if defined, we do NOT check that the stack sizes are empty at the end. 
// You might want this if we are using one VirtualMachineState to compute multiple outputs.
// NOTE: This is risky to disable because if we mess up something in implementation, this check often 
// helps to find it 
//#define NO_CHECK_END_STACK_SIZE 1 

// These are the only types of classes we are able to memoize in a lexicon
// NOTE: We need short because that's the "key" used for LOTHypothesis instead of lexicon
#define LEXICON_MEMOIZATION_TYPES  short,std::string,int 

namespace FleetStatistics {}
template<typename X> class VirtualMachinePool;
extern std::atomic<uintmax_t> FleetStatistics::vm_ops;

/**
 * @class VirtualMachineState
 * @author piantado
 * @date 02/02/20
 * @file VirtualMachineState.h
 * @brief This represents the state of a partial evaluation of a program, corresponding to the value of all of the stacks
 * 			of various types (which are stored as templates from VM_TYPES).  
 *  		The idea here is that we want to be able to encapsulate everything about the evaluation of a tree so 
 * 			that we can stop it in the middle and resume later, as is required for stochastics. This must be 
 * 			templated because it depends on the types in the grammar. 
 * 			These will typically be stored in a VirtualMachinePool and not called directly, unless you know
 * 			that there are no stochastics. 
 */ 
template<typename _t_input, typename _t_output, typename... VM_TYPES>
class VirtualMachineState : public VirtualMachineControl {
public:

	using input_t  = _t_input;
	using output_t = _t_output;
	
	using this_t = VirtualMachineState<input_t, output_t, VM_TYPES...>; 
	
	// Define the function type for instructions and things operating on this VirtualMachineState
	// This is read a few other places, like in Builtins
	using FT = std::function<void(this_t*,int)>;

	// what we use internally for stacks
	template<typename T>
	using VMSStack = Stack<T>;
	
	//static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
	Program<this_t>    program; // programs are instructions for myself
	VMSStack<input_t>  xstack; //xstackthis stores a stack of the x values (for recursive calls)
	const output_t&    err; // what error output do we return? Just a reference to a value for speed
	double             lp; // the probability of this context
	
	unsigned long 	  recursion_depth; // when I was created, what was my depth?
	
private:
	// these are private and should only be accessed via stack(), mem(), memstack() below
	
	// This is a little bit of fancy template metaprogramming that allows us to define a stack
	// like std::tuple<VMSStack<bool>, VMSStack<std::string> > using a list of type names defined in VM_TYPES
	template<typename... args>
	struct stack_t { std::tuple<VMSStack<args>...> value; };
	stack_t<VM_TYPES...> _stack; // our stacks of different types
	
	// same for defining memoization types -- here these are the only ones we allow
	template<typename... args>
	struct mem_t { std::tuple<std::map<std::pair<args,input_t>,output_t>...> value; };
	mem_t<LEXICON_MEMOIZATION_TYPES> _mem;
	
	template<typename... args>
	struct memstack_t { std::tuple< VMSStack<std::pair<args, input_t>>...> value; };
	memstack_t<LEXICON_MEMOIZATION_TYPES> _memstack;

public:

	vmstatus_t status; // are we still running? Did we get an error?
	
	// what do we use to count up instructions 
	// NOTE for now this may be a bit slower and unnecessary but it doesn't seem so bad at the
	// moment so this may need to be optimized later to be optional
	RuntimeCounter runtime_counter;
	
	// where we place random flips back onto
	VirtualMachinePool<this_t>* pool;
	
	VirtualMachineState(input_t x, const output_t& e, VirtualMachinePool<this_t>* po) :
		err(e), lp(0.0), recursion_depth(0), status(vmstatus_t::GOOD), pool(po) {
		xstack.push(x);	
	}
	
	template<typename T>
	std::map<std::pair<T,input_t>,output_t>& mem() { return std::get<std::map<std::pair<T,input_t>,output_t>>(_mem.value); }
 
	template<typename T>
	VMSStack<std::pair<T,input_t>>& memstack() { return std::get<VMSStack<std::pair<T,input_t>>>(_memstack.value); }
	
	/**
	 * @brief These must be sortable by lp so that we can enumerate them from low to high probability in a VirtualMachinePool 
	 * 		  NOTE: VirtualMachineStates shouldn't be put in a set because they might evaluate to equal! 
	 * @param m
	 * @return 
	 */
	auto operator<=>(const VirtualMachineState& m) const {
		return lp <=> m.lp; 
	}
	
	/**
	 * @brief Returns a reference to the stack (of a given type)
	 * @return 
	 */
	template<typename T>
	VMSStack<T>& stack() { 
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		return std::get<VMSStack<T>>(_stack.value); 
	}
	
	/**
	 * @brief Const reference to top of stack
	 * @return 
	 */	
	template<typename T>
	const VMSStack<T>& stack() const { 
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		return std::get<VMSStack<T>>(_stack.value); 
	}
	
	/**
	 * @brief Retrieves and pops the element of type T from the stack
	 * @return 
	 */
	template<typename T>
	T getpop() {
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		T x = std::move(stack<T>().top());
		stack<T>().pop();
		return x;
	}
	
	/**
	* @brief Getpops the n'th element of args (useful for writing primitives)
	* @return 
	*/
	template<size_t n, typename...args>
	auto getpop_nth() {
		static_assert( n >= 0 and n < sizeof...(args) && "*** Higher n than args.");
		return getpop<typename std::tuple_element<n, std::tuple<args...> >::type>();
	}
	
	/**
	* @brief Retrieves the top of the stack as a copy and does *not* remove
	* @return 
	*/
	template<typename T>
	T gettop() {
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		assert(stack<T>().size() > 0 && "Cannot gettop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		return stack<T>().top();
	}
		
	template<typename T>
	void push(T& x){
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		/**
		 * @brief Push things onto the appropriate stack
		 * @param x
		 */
		stack<T>().push(x);
	}
	template<typename T>
	void push(T&& x){
		static_assert(contains_type<T,VM_TYPES...>() && "*** Error type T missing from VM_TYPES");
		stack<T>().push(std::move(x));
	}

	void push_x(input_t x) {
		xstack.push(x);
	}

	/**
	 * @brief There is one element in stack T and the rest are empty. Used to check in returning the output.
	 * @return 
	 */
	template<typename T, typename... args>
	bool _exactly_one() const {
		return (... && ((std::is_same<T,args>::value and stack<T>().size()==1) or stack<args>().empty())); 
	}
	template<typename T>
	bool exactly_one() const {
		return this->_exactly_one<T, VM_TYPES...>();
	}
	
	/**
	 * @brief Return the output and do some checks that the stacks are as they should be if you're reading the output.
	 * @return 
	 */	
	output_t get_output() {
		
		if(status == vmstatus_t::ERROR) 
			return err;		
		
		assert(status == vmstatus_t::COMPLETE && "*** Probably should not be calling this unless we are complete");
		
		#ifndef NO_CHECK_END_STACK_SIZE
		assert( exactly_one<output_t>() and xstack.size() == 1 and "When we return, all of the stacks should be empty or else something is awry.");
		#endif 
		
		return gettop<output_t>();
	}
	
	/**
	 * @brief Run with a pointer back to pool p. This is required because "flip" may push things onto the pool.
	 * 		  Note that here we allow a tempalte on HYP, which actually gets passed all the way down to 
	 *        applyPrimitives, which means that whatever arguments we give here are passed all teh way back 
	 *        to the VMS Primitives when they are called. They really should be loaders, but they could be anything. 
	 * @param pool
	 * @param loader
	 * @return 
	 */	
	 output_t run() {
		status = vmstatus_t::GOOD;
		
		try { 
			
			while(status == vmstatus_t::GOOD and (not program.empty()) ) {
				
				if(program.size() + runtime_counter.total > MAX_RUN_PROGRAM ) {  // if we've run too long or we couldn't possibly finish
					//print("HEEERE");
					throw VMSRuntimeError();
				}
				
				FleetStatistics::vm_ops++;
				
				Instruction i = program.top(); program.pop();
				
				// keep track of what instruction we've run
				runtime_counter.increment(i);
				
				auto f = reinterpret_cast<FT*>(i.f);
				(*f)(const_cast<this_t*>(this), i.arg);
				 
			} // end while loop over ops
			
		} catch (VMSRuntimeError& e) {
			// this may be thrown by a primitive
			status = vmstatus_t::ERROR;
		}
		
		// when we exit, set the status to complete if we are good
		// otherwise, leave it where it was
		if(status == vmstatus_t::GOOD) {
			status = vmstatus_t::COMPLETE;
			return get_output();
		}
		else {
			// if we get here, there was a problem 
			return err;
		}
	}	
	
};
