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
#include "Hypotheses/Interfaces/ProgramLoader.h"
#include "VirtualMachineControl.h"

#include "VMSRuntimeError.h"

/**
 * @class has_operator_lessthan_impl
 * @author piantado
 * @date 07/05/20
 * @file VirtualMachineState.h
 * @brief See if a class implements operator< (for filtering out in op_MEM code so it doesn't give an error if we use input_t that doesn't implement operator<
 * 		  as long as no op_MEM is called
 */
template<class T, class EqualTo>
struct has_operator_lessthan_impl {
    template<class U, class V>
    static auto test(U*) -> decltype(std::declval<U>() < std::declval<V>());
    template<typename, typename>
    static auto test(...) -> std::false_type;

    using type = typename std::is_same<bool, decltype(test<T, EqualTo>(0))>::type;
};

template<class T, class EqualTo = T>
struct has_operator_lessthan : has_operator_lessthan_impl<T, EqualTo>::type {};
// https://stackoverflow.com/questions/6534041/how-to-check-whether-operator-exists



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

	template<typename T>
	class VMSStack : public Stack<T> {		
		// This is a kind of stack that just reserves some for VirtualMachineStates (if that's faster, which its not)
	public:
		VMSStack() {
			// this->reserve(8);
		}
	};
	
public:

	typedef _t_input  input_t;
	typedef _t_output output_t;
	
	typedef VirtualMachineState<input_t,output_t, VM_TYPES...> this_t; 
	
	//static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
	Program            opstack; 
	VMSStack<input_t>  xstack; //xstackthis stores a stack of the x values (for recursive calls)
	output_t           err; // what error output do we return?
	double             lp; // the probability of this context
	
	unsigned long 	  recursion_depth; // when I was created, what was my depth?
	
		// This is a little bit of fancy template metaprogramming that allows us to define a stack
	// like std::tuple<VMSStack<bool>, VMSStack<std::string> > using a list of type names defined in VM_TYPES
	template<typename... args>
	struct t_stack { std::tuple<VMSStack<args>...> value; };
	t_stack<VM_TYPES...> _stack; // our stacks of different types
	
	typedef int index_t; // how we index into factorized lexica -- NOTE: probably should be castable from Instruction.arg 
	
	// must have a memoized return value, that permits factorized by requiring an index argument
	std::map<std::pair<index_t, input_t>, output_t> mem; 

	// when we recurse and memoize, this stores the arguments (index and input_t) for us to 
	// rember after the program trace is done
	VMSStack<std::pair<index_t, input_t>> memstack;

	vmstatus_t status; // are we still running? Did we get an error?
	
	// what do we use to count up instructions 
	// NOTE for now this may be a bit slower and unnecessary but it doesnt' seem so bad at the
	// moment so this may need to be optimized later to be optional
	RuntimeCounter runtime_counter;
	
	// what we use to load programs
	ProgramLoader* program_loader;
	
	// where we place random flips back onto
	VirtualMachinePool<this_t>* pool;
	
	VirtualMachineState(input_t x, output_t e, ProgramLoader* pl, VirtualMachinePool<this_t>* po) :
		err(e), lp(0.0), recursion_depth(0), status(vmstatus_t::GOOD), program_loader(pl), pool(po) {
		xstack.push(x);	
	}
	
	virtual ~VirtualMachineState() {}	// needed so VirtualMachinePool can delete

	/**
	 * @brief These must be sortable by lp so that we can enumerate them from low to high probability in a VirtualMachinePool 
	 * 		  NOTE: VirtualMachineStates shouldn't be put in a set because they might evaluate to equal! 
	 * @param m
	 * @return 
	 */
	bool operator<(const VirtualMachineState& m) const {
		return lp < m.lp; 
	}

	/**
	 * @brief Add v to my lp
	 * @param v
	 */
	void increment_lp(double v) {
		lp += v;
	}
	
	/**
	 * @brief How many more steps can I be run for? This is useful for not adding programs that contain too many ops
	 * @return 
	 */		
	unsigned long remaining_steps() {
		return MAX_RUN_PROGRAM - runtime_counter.total;
	}
	
	/**
	 * @brief Returns a reference to the stack (of a given type)
	 * @return 
	 */
	template<typename T>
	VMSStack<T>& stack()             { 
		return std::get<VMSStack<T>>(_stack.value); 
	}
	
	/**
	 * @brief Const reference to top of stack
	 * @return 
	 */	
	template<typename T>
	const VMSStack<T>& stack() const { 
		return std::get<VMSStack<T>>(_stack.value); 
	}
	
	/**
	 * @brief Retrieves and pops the element of type T from the stack
	 * @return 
	 */
	template<typename T>
	T getpop() {
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		T x = std::move(stack<T>().top());
		stack<T>().pop();
		return x;
	}
	
	/**
	* @brief Retrieves the top of the stack as a copy and does *not* remove
	* @return 
	*/
	template<typename T>
	T gettop() {
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		return stack<T>().top();
	}
	/**
	* @brief Retrieves the top of the stack as a copy and does *not* remove
	* @return 
	*/	template<typename T>
	T& gettopref() {
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		return stack<T>().topref();
	}
		
	/**
	 * @brief This is some fanciness that will return a reference to the top of the stack if we give it a reference type 
	 * 			otherwise it will return the type. This lets us get the top of a stack with a reference in PRIMITIVES
	 * 			as though we were some kind of wizards
	 * @return 
	 */		
	template<typename T>
	typename std::conditional<std::is_reference<T>::value, T&, T>::type
	get() {
		using Tdecay = typename std::decay<T>::type; // remove the ref from it since that's how we access the stack -- TODO: put this into this->stack() maybe?
		
		assert(stack<Tdecay>().size() > 0 && "Cannot get from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		// some magic: if its a reference, we return a reference to the top of the stack
		// otherwise we move off and return
		if constexpr (std::is_reference<T>::value) { 
			// if its a reference, reference the un-referenced stack type and return a reference to its top
			// NOTE: It is important that this does not pop, because that means it doesn't matter when we call it
			// in Primitives
			return std::forward<T>(stack<Tdecay>().topref());
		}
		else {
			T x = std::move(stack<Tdecay>().top());
			stack<Tdecay>().pop();
			return x;
		}
	}

	/**
	 * @brief Is this stack empty?
	 * @return 
	 */
	template<typename T>
	bool empty() {		
		return stack<T>().empty();
	}

	template<typename T>
	void push(T& x){
		/**
		 * @brief Push things onto the appropriate stack
		 * @param x
		 */
		stack<T>().push(x);
	}
	template<typename T>
	void push(T&& x){
		stack<T>().push(std::move(x));
	}
	template<typename... args>
	bool any_stacks_empty() const { 
		/**
		 * @brief Check if any of the stacks are empty
		 * @return 
		 */		
		return (... || stack<args>().empty()); 
	}
	template<typename... args>
	bool all_stacks_empty() const { 
		/**
		 * @brief Check if all of the stacks are empty (should be at the end of evaluation)
		 * @return 
		 */
		return (... && stack<args>().empty()); 
	}	
	bool stacks_empty() const { 
		/**
		 * @brief True if all stacks are empty for the VM_TYPES
		 * @return 
		 */
		return this->all_stacks_empty<VM_TYPES...>();
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
		return this->_exactly_one<output_t, VM_TYPES...>();
	}
	
	/**
	 * @brief Return the output and do some checks that the stacks are as they should be if you're reading the output.
	 * @return 
	 * 		// TODO: FIX THIS SO IT DOESN'T POP FROM OUTPUT_T
	 */	
	output_t get_output() {
		
		if(status == vmstatus_t::ERROR) {
			return err;		
		}
		
		assert(status == vmstatus_t::COMPLETE && "*** Probably should not be calling this unless we are complete");
		assert( exactly_one<output_t>() and xstack.size() == 1 and "When we return, all of the stacks should be empty or else something is awry.");
		
		return gettop<output_t>();
	}
	
	template<typename HYP>
	output_t run(HYP* d) {
		/**
		 * @brief Defaultly run a non-random hypothesis
		 * @param d
		 * @return 
		 */
		return run(nullptr, d);
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
			
			while(status == vmstatus_t::GOOD and (not opstack.empty()) ) {
				
				if(opstack.size() > remaining_steps() ) {  // if we've run too long or we couldn't possibly finish
					status = vmstatus_t::RUN_TOO_LONG;
					break;
				}
				
				FleetStatistics::vm_ops++;
				
				Instruction i = opstack.top(); opstack.pop();
				
				// keep track of what instruction we've run
				runtime_counter.increment(i);
				
				// Now actually dispatch for whatever i is doing to this stack
				if(i.is<PrimitiveOp>()) {
					if constexpr(sizeof...(VM_TYPES) > 0) {
						// call this fancy template magic to index into the global tuple variable PRIMITIVES
						status = applyPRIMITIVEStoVMS(i.as<PrimitiveOp>(), this, pool, program_loader);
					}
					else {
						throw YouShouldNotBeHereError("*** Cannot call PrimitiveOp without defining VM_TYPES");
					}
				}
				else {
					assert(i.is<BuiltinOp>());
					
					switch(i.as<BuiltinOp>()) {
						case BuiltinOp::op_NOP: 
						{
							break;
						}
						case BuiltinOp::op_X:
						{
							assert(!xstack.empty());
							push<input_t>(std::move(xstack.top()));
							break;
						}
						case BuiltinOp::op_POPX:
						{
							xstack.pop(); // NOTE: Remember NOT to pop from getpop<input_t>() since that's not where x is stored
							break;
						}
						case BuiltinOp::op_ALPHABET: 
						{
							if constexpr (contains_type<std::string,VM_TYPES...>()) { 
								// convert the instruction arg to a string and push it
								push(std::move(std::string(1,(char)i.arg)));
								break;
							}
							else { throw YouShouldNotBeHereError("*** Cannot use op_ALPHABET if std::string is not in VM_TYPES"); }
						}
						case BuiltinOp::op_ALPHABETchar: 
						{
							if constexpr (contains_type<char,VM_TYPES...>()) { 
								// convert the instruction arg to a string and push it
								push((char)i.arg);
								break;
							}
							else { throw YouShouldNotBeHereError("*** Cannot use op_ALPHABET if char is not in VM_TYPES"); }
						}
						case BuiltinOp::op_INT: 
						{
							if constexpr (contains_type<int,VM_TYPES...>()) { 
								// convert the instruction arg to a string and push it
								push((int)i.arg);
								break;
							}
							else { throw YouShouldNotBeHereError("*** Cannot use op_INT if int is not in VM_TYPES"); }
						}	
						case BuiltinOp::op_FLOAT: 
						{
							if constexpr (contains_type<float,VM_TYPES...>()) { 
								// convert the instruction arg to a string and push it
								push((float)i.arg);
								break;
							}
							else { throw YouShouldNotBeHereError("*** Cannot use op_FLOAT if float is not in VM_TYPES"); }
						}				
						case BuiltinOp::op_P: {
							if constexpr (contains_type<double,VM_TYPES...>()) { 
								push( double(i.arg)/double(Pdenom) );
								break;
							} 
							else { throw YouShouldNotBeHereError("*** Cannot use op_P if double is not in VM_TYPES"); }

						}					
						case BuiltinOp::op_MEM:
						{
							// Let's not make a big deal when 
							if constexpr (has_operator_lessthan<input_t>::value) {
								
								auto v = gettop<output_t>(); // what I should memoize should be on top here, but don't remove because we also return it
								auto memindex = memstack.top(); memstack.pop();
								if(mem.count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrte if you have
									mem[memindex] = v;
								}
								
								break;
								
							} else { throw YouShouldNotBeHereError("*** Cannot call op_MEM without defining operator< on input_t"); }
						}
						case BuiltinOp::op_TRUE: 
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								push<bool>(true);
							} else { throw YouShouldNotBeHereError("*** Must have bool defined to use op_TRUE");}
							break;
						}
						case BuiltinOp::op_FALSE: 
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								push<bool>(false);
							} else { throw YouShouldNotBeHereError("*** Must have bool defined to use op_FALSE");}
							break;
						}
						case BuiltinOp::op_SAFE_RECURSE: {
							// This is a recursion that returns empty for empty arguments 
							// simplifying the condition
							if constexpr (std::is_same<input_t, std::string>::value) { 						

								assert(program_loader != nullptr);
								
								// need to check if stack<input_t> is empty since thats where we get x
								if (empty<input_t>()) {
									push<output_t>(output_t{});
									continue;
								}
								else if(stack<input_t>().top().size() == 0) { 
									getpop<input_t>(); // this would have been the argument
									push<output_t>(output_t{}); //push default (null) return
									continue;
								}
								else if(recursion_depth+1 > MAX_RECURSE) {
									getpop<input_t>(); // ignored b/c we're bumping out
									push<output_t>(output_t{});
									continue;
								}
							} else { throw YouShouldNotBeHereError("*** Can only use SAFE_RECURSE on strings");}
							
							// want to fallthrough here
							[[fallthrough]];
						}
						case BuiltinOp::op_RECURSE:
						{
							
							assert(program_loader != nullptr);
							
							if(recursion_depth++ > MAX_RECURSE) { // there is one of these for each recurse
								status = vmstatus_t::RECURSION_DEPTH;
								return err;
							}
							else if( program_loader->program_size(i.getArg()) + opstack.size() > remaining_steps()) { // check if we have too many
								status = vmstatus_t::RUN_TOO_LONG;
								return err;
							}
							// if we get here, then we have processed our arguments and they are stored in the input_t stack. 
							// so we must move them to the x stack (where there are accessible by op_X)
							auto mynewx = getpop<input_t>();
							xstack.push(std::move(mynewx));
							opstack.push(Instruction(BuiltinOp::op_POPX)); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
							
							// push this program 
							// but we give i.arg so that we can pass factorized recursed
							// in argument if we want to
							program_loader->push_program(opstack,i.getArg()); 
							
							// after execution is done, the result will be pushed onto output_t
							// which is what gets returned when we are all done
							
							break;
						}
						case BuiltinOp::op_SAFE_MEM_RECURSE: {
							// same as SAFE_RECURSE. Note that there is no memoization here
							if constexpr (std::is_same<input_t,std::string>::value and has_operator_lessthan<input_t>::value) {				
								
								assert(program_loader != nullptr);
								
								// Here we don't need to memoize because it will always give us the same answer!
								if (empty<input_t>()) {
									push<output_t>(output_t{});
									continue;
								}
								else if(stack<input_t>().top().size() == 0) { //exists but is empty
									getpop<input_t>(); // this would have been the argument
									push<output_t>(output_t{}); //push default (null) return
									continue;
								}
								else if(recursion_depth+1 > MAX_RECURSE) {
									getpop<input_t>();
									push<output_t>(output_t{});
									continue;
								}
							} else { throw YouShouldNotBeHereError("*** Can only use SAFE_MEM_RECURSE on strings.");}

							// want to fallthrough here
							[[fallthrough]];
						}
						case BuiltinOp::op_MEM_RECURSE:
						{
							if constexpr (has_operator_lessthan<input_t>::value) {
								if(recursion_depth++ > MAX_RECURSE) {
									status = vmstatus_t::RECURSION_DEPTH;
									return err;
								}
								else if( program_loader->program_size(i.getArg()) + opstack.size() > remaining_steps()) { // check if we have too many
									status = vmstatus_t::RUN_TOO_LONG;
									return err;
								}
								assert(program_loader != nullptr);
								
								input_t x = getpop<input_t>(); // get the argument
							
								std::pair<index_t,input_t> memindex(i.getArg(),x);
								
								if(mem.count(memindex)){
									push(std::move(mem[memindex])); 
								}
								else {	
									xstack.push(x);	
									memstack.push(memindex); // popped off by op_MEM
									opstack.push(Instruction(BuiltinOp::op_MEM));
									opstack.push(Instruction(BuiltinOp::op_POPX));
									program_loader->push_program(opstack,i.getArg()); // this leaves the answer on top
								}
								
								break;						
								
							} else { throw YouShouldNotBeHereError("*** Cannot MEM_RECURSE unless input_t has operator< defined"); }
						}
						
						// Both flips come here so we don't duplicate code
						// and then we sort out whether the p is a default value 
						// or not				
						case BuiltinOp::op_FLIP: 
						{
							[[fallthrough]];
						}
						case BuiltinOp::op_FLIPP:
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								assert(pool != nullptr && "op_FLIP and op_FLIPP require the pool to be non-null, since they push onto the pool. Maybe you used callOne instead of call?"); // can't do that, for sure
						
								double p = 0.5; 
								
								if constexpr (contains_type<double,VM_TYPES...>()) {  // if we have double allowed we cna do this
									if(i.is_a(BuiltinOp::op_FLIPP)) { // only for built-in ops do we 
										p = getpop<double>(); // reads a double argfor the coin weight
										if(std::isnan(p)) { p = 0.0; } // treat nans as 0s
										assert(p <= 1.0 and p >= 0.0);
									}
								} else { assert( (!i.is_a(BuiltinOp::op_FLIPP) && "*** Cannot us flipp without double defined")); } // if there is no double, we can't use flipp
								
					
								pool->copy_increment_push(this, true,  log(p));
	//							pool->copy_increment_push(*this, false,  log(1.0-p));
								bool b = pool->increment_push(this, false, log(1.0-p)); 
								
								// TODO: This is clumsy, ugly mechanism -- need to re-do
								
								// since we pushed this back onto the queue (via increment_push), we need to tell the 
								// pool not to delete this, so we send back this special signal
								if(b) { // theoutcome of increment_push decides whether I am deleted or not
									status = vmstatus_t::RANDOM_CHOICE_NO_DELETE; 
								}
								else {
									status = vmstatus_t::RANDOM_CHOICE; 
								}
								
								// TODO: We can also keep running here -- though its more complex. 
								
								break;
		
							} else { throw YouShouldNotBeHereError("*** Cannot use op_FLIP without defining bool in VM_TYPES"); }
						}
						case BuiltinOp::op_JMP:
						{
							opstack.popn(i.arg);
							break;
						}
						case BuiltinOp::op_IF: 
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								// Here we evaluate op_IF, which has to short circuit and skip (pop some of the stack) 
								bool b = getpop<bool>(); // bool has already evaluted
								
								// now ops must skip the xbranch
								if(!b) opstack.popn(i.arg);
								else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
								
								break;		
							} else { throw YouShouldNotBeHereError("*** Cannot use op_IF without defining bool in VM_TYPES"); }			
						}
						case BuiltinOp::op_AND:
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								// process the short circuit
								bool b = getpop<bool>(); // bool has already evaluted
								
								if(!b) {
									opstack.popn(i.arg); // pop off the other branch 
									push<bool>(false); //  entire and must be false
								}
								else {
									// else our value is just the other value -- true when its true and false when its false
								}
								
								break;		
							} else { throw YouShouldNotBeHereError("*** Cannot use op_AND without defining bool in VM_TYPES"); }		
						}
						case BuiltinOp::op_OR:
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								// process the short circuit
								bool b = getpop<bool>(); // bool has already evaluted
								
								if(b) {
									opstack.popn(i.arg); // pop off the other branch 
									push<bool>(true); //  entire and must be false
								}
								else {
									// else our value is just the other value -- true when its true and false when its false
								}
								
								break;		
							} else { throw YouShouldNotBeHereError("*** Cannot use op_OR without defining bool in VM_TYPES"); }							
						}
						case BuiltinOp::op_NOT: // we want to include just for simplicity
						{
							if constexpr (contains_type<bool,VM_TYPES...>()) { 
								bool b = getpop<bool>();
								push<bool>(not b);
								break;		
							} else { throw YouShouldNotBeHereError("*** Cannot use op_NOT without defining bool in VM_TYPES"); }		
							
						}
						case BuiltinOp::op_I:
						case BuiltinOp::op_S:
						case BuiltinOp::op_K:
						case BuiltinOp::op_SKAPPLY: {
								throw YouShouldNotBeHereError("*** Should not call these -- instead use Fleet::Combinator::Reduce");
						}												
						default: 
						{
							throw YouShouldNotBeHereError("Bad op name");
						}
					} // end switch
				
				} // end if not custom
			} // end while loop over ops
	
			// and when we exit, set the status to complete if we are good
			// otherwise, leave it where it was!
			if(status == vmstatus_t::GOOD) {
				status = vmstatus_t::COMPLETE;
				return get_output();
			}
			else {
				return err;
			}
			
		} catch (VMSRuntimeError& e) {
			// this may be thrown by a primitive
			status = vmstatus_t::ERROR;
			
			return err;
		}
		
	}	
	
};
