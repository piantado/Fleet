#pragma once

#include <type_traits>

// Remove n from the stack
void popn(Program& s, size_t n) {
	for(size_t i=0;i<n;i++) s.pop();
}

/**
 * @class VirtualMachineState
 * @author piantado
 * @date 02/02/20
 * @file VirtualMachineState.h
 * @brief This represents the state of a partial evaluation of a program, corresponding to the value of all of the stacks
 * 			of various types (which are stored as templates from FLEET_GRAMMAR_TYPES).  
 *  		The idea here is that we want to be able to encapsulate everything about the evaluation of a tree so 
 * 			that we can stop it in the middle and resume later, as is required for stochastics. This must be 
 * 			templated because it depends on the types in the grammar. 
 * 			These will typically be stored in a VirtualMachinePool and not called directly, unless you know
 * 			that there are no stochastics. 
 */
template<typename t_x, typename t_return>
class VirtualMachineState {
	
public:
	
	static const unsigned long MAX_RECURSE = 64; // this is the max number of times we can call recurse, NOT the max depth
	//static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
	Program            opstack; 
	Stack<t_x>         xstack; //xstackthis stores a stack of the x values (for recursive calls)
	t_return           err; // what error output do we return?
	double             lp; // the probability of this context
	
	unsigned long 	  recursion_depth; // when I was created, what was my depth?

	// This is a little bit of fancy template metaprogramming that allows us to define a stack
	// like std::tuple<Stack<bool>, Stack<std::string> > using a list of type names defined in FLEET_GRAMMAR_TYPES
	template<typename... args>
	struct t_stack { std::tuple<Stack<args>...> value; };
	t_stack<FLEET_GRAMMAR_TYPES> _stack; // our stacks of different types
	
	typedef int index_t; // how we index into factorized lexica -- NOTE: probably should be castable from Instruction.arg 
	
	// must have a memoized return value, that permits factorized by requiring an index argument
	std::map<std::pair<index_t, t_x>, t_return> mem; 

	// when we recurse and memoize, this stores the arguments (index and t_x) for us to 
	// rember after the program trace is done
	Stack<std::pair<index_t, t_x>> memstack;

	vmstatus_t status; // are we still running? Did we get an error?
	
	VirtualMachineState(t_x x, t_return e, size_t _recursion_depth=0) :
		err(e), lp(0.0), recursion_depth(_recursion_depth), status(vmstatus_t::GOOD) {
		xstack.push(x);	
	}
	
	virtual ~VirtualMachineState() {}	// needed so VirtualMachinePool can delete
	
	bool operator<(const VirtualMachineState& m) const {
		/**
		 * @brief These must be sortable by lp so that we can enumerate them from low to high probability in a VirtualMachinePool 
		 * 		  NOTE: VirtualMachineStates shouldn't be put in a set because they might evaluate to equal! 
		 * @param m
		 * @return 
		 */
		return lp < m.lp; 
	}
	
	void increment_lp(double v) {
		/**
		 * @brief Add v to my lp
		 * @param v
		 */
		
		lp += v;
	}
	
	// Functions to access the stack
	template<typename T>
	Stack<T>& stack()             { 
		/**
		 * @brief Returns a reference to the stack (of a given type)
		 * @return 
		 */
		return std::get<Stack<T>>(_stack.value); 
	}
	template<typename T>
	const Stack<T>& stack() const { 
		/**
		 * @brief Const reference to top of stack
		 * @return 
		 */		
		return std::get<Stack<T>>(_stack.value); 
	}
	
	template<typename T>
	T getpop() {
		/**
		 * @brief Retrieves and pops the element of type T from the stack
		 * @return 
		 */
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		T x = std::move(stack<T>().top());
		stack<T>().pop();
		return x;
	}
	template<typename T>
	T gettop() {
		/**
		* @brief Retrieves the top of the stack as a copy and does *not* remove
		* @return 
		*/
		assert(stack<T>().size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		return stack<T>().top();
	}

	template<typename T>
	typename std::conditional<std::is_reference<T>::value, T&, T>::type
	get() {
		/**
		 * @brief This is some fanciness that will return a reference to the top of the stack if we give it a reference type 
		 * 			otherwise it will return the type. This lets us get the top of a stack with a reference in PRIMITIVES
		 * 			as though we were some kind of wizards
		 * @return 
		 */		
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
//			CERR "POPPING ONE " TAB x  ENDL;
			stack<Tdecay>().pop();
			return x;
		}
	}
	
	template<typename T>
	bool empty() {
		/**
		 * @brief Is this stack empty?
		 * @return 
		 */
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
		stack<T>().push(x);
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
		 * @brief True if all stacks are empty for the FLEET_GRAMMAR_TYPES
		 * @return 
		 */
		return this->all_stacks_empty<FLEET_GRAMMAR_TYPES>();
	}
	
	virtual t_return run(Dispatchable<t_x,t_return>* d) {
		/**
		 * @brief Defaultly run a non-recursive hypothesis
		 * @param d
		 * @return 
		 */
		return run(nullptr, d, d);
	}
	
	virtual t_return run(VirtualMachinePool<t_x, t_return>* pool, Dispatchable<t_x,t_return>* dispatch, Dispatchable<t_x,t_return>* loader) {
		/**
		 * @brief Run with a pointer back to pool p. This is required because "flip" may push things onto the pool.
		 * 			Here, dispatch is called to evaluate the function, and loader is called on recursion (allowing us to handle recursion
		 * 			via a lexicon or just via a LOTHypothesis). NOTE that anything NOT built-in is handled via applyToVMS defined in Primitives.h
		 * @param pool
		 * @param dispatch
		 * @param loader
		 * @return 
		 */

		status = vmstatus_t::GOOD;
		
		try { 
			
			while(!opstack.empty()){
				if(status != vmstatus_t::GOOD) return err;
				FleetStatistics::vm_ops++;
				
				Instruction i = opstack.top(); opstack.pop();
				if(i.is<CustomOp>()) {
					status= dispatch->dispatch_custom(i, pool, this, loader);
				}
				else if(i.is<PrimitiveOp>()) {
					
					// call this fancy template magic to index into the global tuple variable PRIMITIVES
					status = applyToVMS(PRIMITIVES, i.as<PrimitiveOp>(), this, pool, loader);
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
							push<t_x>(xstack.top());
							break;
						}
						case BuiltinOp::op_POPX:
						{
							xstack.pop(); // NOTE: Remember NOT to pop from getpop<t_x>() since that's not where x is stored
							break;
						}
						case BuiltinOp::op_ALPHABET: 
						{
							if constexpr (contains_type<std::string,FLEET_GRAMMAR_TYPES>()) { 
								// convert the instruction arg to a string and push it
								push(std::string(1,(char)i.arg));
								break;
							}
							else { assert(false && "*** Cannot use op_ALPHABET if std::string is not in FLEET_GRAMMAR_TYPES"); }
						}
						case BuiltinOp::op_ALPHABETchar: 
						{
							if constexpr (contains_type<char,FLEET_GRAMMAR_TYPES>()) { 
								// convert the instruction arg to a string and push it
								push((char)i.arg);
								break;
							}
							else { assert(false && "*** Cannot use op_ALPHABET if std::string is not in FLEET_GRAMMAR_TYPES"); }
						}
						case BuiltinOp::op_INT: 
						{
							if constexpr (contains_type<int,FLEET_GRAMMAR_TYPES>()) { 
								// convert the instruction arg to a string and push it
								push((int)i.arg);
								break;
							}
							else { assert(false && "*** Cannot use op_INT if std::string is not in FLEET_GRAMMAR_TYPES"); }
						}			
						case BuiltinOp::op_P: {
							if constexpr (contains_type<double,FLEET_GRAMMAR_TYPES>()) { 
								push( double(i.arg)/double(Fleet::Pdenom) );
								break;
							} 
							else { assert(false && "*** Cannot use op_P if std::string is not in FLEET_GRAMMAR_TYPES"); }

						}					
						case BuiltinOp::op_MEM:
						{
							// Let's not make a big deal when 
							if constexpr (has_operator_lessthan<t_x>::value) {
								
								auto v = gettop<t_return>(); // what I should memoize should be on top here, but dont' remove because we also return it
								auto memindex = memstack.top(); memstack.pop();
								if(mem.count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrte if you have
									mem[memindex] = v;
								}
								
								break;
								
							} else { assert(0 && "*** Cannot call op_MEM without defining operator< on t_input"); }
						}
						case BuiltinOp::op_TRUE: 
						{
							if constexpr (contains_type<bool,FLEET_GRAMMAR_TYPES>()) { 
								push<bool>(true);
							} else { assert(0 && "*** Must have bool defined to use op_TRUE");}
							break;
						}
						case BuiltinOp::op_FALSE: 
						{
							if constexpr (contains_type<bool,FLEET_GRAMMAR_TYPES>()) { 
								push<bool>(false);
							} else { assert(0 && "*** Must have bool defined to use op_FALSE");}
							break;
						}
						case BuiltinOp::op_SAFE_RECURSE: {
							// This is a recursion that returns empty for empty arguments 
							// simplifying the condition
							if constexpr (std::is_same<t_x, std::string>::value) { 						

								// need to check if stack<t_x> is empty since thats where we get x
								if (empty<t_x>()) {
									push<t_return>(t_return{});
									continue;
								}
								else if(stack<t_x>().top().size() == 0) { 
									getpop<t_x>(); // this would have been the argument
									push<t_return>(t_return{}); //push default (null) return
									continue;
								}
								else if(recursion_depth+1 > MAX_RECURSE) {
									getpop<t_x>(); // ignored b/c we're bumping out
									push<t_return>(t_return{});
									continue;
								}
							} else { assert(false && "*** Can only use SAFE_RECURSE on strings");}
							
							// want to fallthrough here
							[[fallthrough]];
						}
						case BuiltinOp::op_RECURSE:
						{
							
							if(recursion_depth++ > MAX_RECURSE) { // there is one of these for each recurse
								status = vmstatus_t::RECURSION_DEPTH;
								return err;
							}
							
							// if we get here, then we have processed our arguments and they are stored in the t_x stack. 
							// so we must move them to the x stack (where there are accessible by op_X)
							xstack.push(getpop<t_x>());
							opstack.push(Instruction(BuiltinOp::op_POPX)); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
							
							// push this program 
							// but we give i.arg so that we can pass factorized recursed
							// in argument if we want to
							loader->push_program(opstack,i.arg); 
							
							// after execution is done, the result will be pushed onto t_return
							// which is what gets returned when we are all done
							
							break;
						}
						case BuiltinOp::op_SAFE_MEM_RECURSE: {
							// same as SAFE_RECURSE. Note that there is no memoization here
							if constexpr (std::is_same<t_x,std::string>::value and has_operator_lessthan<t_x>::value) {				
								// Here we don't need to memoize because it will always give us the same answer!
								if (empty<t_x>()) {
									push<t_return>(t_return{});
									continue;
								}
								else if(stack<t_x>().top().size() == 0) { //exists but is empty
									getpop<t_x>(); // this would have been the argument
									push<t_return>(t_return{}); //push default (null) return
									continue;
								}
								else if(recursion_depth+1 > MAX_RECURSE) {
									getpop<t_x>();
									push<t_return>(t_return{});
									continue;
								}
							} else { assert(false && "*** Can only use SAFE_MEM_RECURSE on strings.");}

							// want to fallthrough here
							[[fallthrough]];
						}
						case BuiltinOp::op_MEM_RECURSE:
						{
							if constexpr (has_operator_lessthan<t_x>::value) {
								if(recursion_depth++ > MAX_RECURSE) {
									status = vmstatus_t::RECURSION_DEPTH;
									return err;
								}
								
								t_x x = getpop<t_x>(); // get the argument
							
								std::pair<index_t,t_x> memindex(i.getArg(),x);
								
								if(mem.count(memindex)){
									push(mem[memindex]); 
								}
								else {	
									xstack.push(x);	
									memstack.push(memindex); // popped off by op_MEM
									opstack.push(Instruction(BuiltinOp::op_MEM));
									opstack.push(Instruction(BuiltinOp::op_POPX));
									loader->push_program(opstack,i.arg); // this leaves the answer on top
								}
								
								break;						
								
							} else { assert(false && "*** Cannot MEM_RECURSE unless t_x has operator< defined"); }
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
							if constexpr (contains_type<bool,FLEET_GRAMMAR_TYPES>()) { 
								assert(pool != nullptr && "op_FLIP and op_FLIPP require the pool to be non-null, since they push onto the pool"); // can't do that, for sure
						
								double p = 0.5; 
								
								if constexpr (contains_type<double,FLEET_GRAMMAR_TYPES>()) {  // if we have double allowed we cna do this
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

					
								// which branch did we take?
	//							bool decision = (p >= 0.5); 
	//							
	//							// save (possibly) the path not taken
	//							pool->copy_increment_push(*this, not decision, log( MIN(p,1.0-p) ));
	//							
	//							// if I am still ok in terms of LP_BREAKOUT, push and keep running
	//							double decisionlp = log(MAX(p, 1.0-p));
	//							if(lp+decisionlp > MAX(pool->min_lp, pool->Q.top().lp - LP_BREAKOUT) ) { // if we can keep going and stay within the bound
	////							if(pool->wouldIadd(lp+decisionlp)){ // if I would be added here (a little imprecise because I may be allowing myself to run for too many steps)
	//								increment_lp(decisionlp);
	//								push<bool>(decision);
	//								// and just continue -- no need to do anything to the stack
	//							}
	//							else {
	//								
	//								// else we just push and return control to the stack
	//								pool->copy_increment_push(std::move(*this), decision, decisionlp);
	//								status = vmstatus_t::RANDOM_CHOICE;
	//							}

								break;
		
							} else { assert(0 && "*** Cannot use op_FLIP without defining bool in FLEET_GRAMMAR_TYPES"); }
						}
						case BuiltinOp::op_JMP:
						{
							popn(opstack, i.arg);
							break;
						}
						case BuiltinOp::op_IF: 
						{
							if constexpr (contains_type<bool,FLEET_GRAMMAR_TYPES>()) { 
								// Here we evaluate op_IF, which has to short circuit and skip (pop some of the stack) 
								bool b = getpop<bool>(); // bool has already evaluted
								
								// now ops must skip the xbranch
								if(!b) popn(opstack, i.arg);
								else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
								
								break;		
							} else { assert(0 && "*** Cannot use op_IF without defining bool in FLEET_GRAMMAR_TYPES"); }			
						}
						default: 
						{
							// otherwise call my dispatch and return if there is an error 
							assert(0 && "Bad op name");
						}
					} // end switch
				
				} // end if not custom
			} // end while loop over ops
		
		} catch (VMSRuntimeError_t& e) {
			// this may be thrown by a primitive
			status = vmstatus_t::ERROR;
			return err; // don't go below because then you'll assert stacks_empty() which they needn't be
		}
	
		if(status != vmstatus_t::GOOD) 
			return err;		
	
		auto ret = getpop<t_return>();
		
		assert(stacks_empty() and xstack.size() == 1 and "When we return, all of the stacks should be empty or else something is awry.");
		
		return ret; 
	}	
	
};
