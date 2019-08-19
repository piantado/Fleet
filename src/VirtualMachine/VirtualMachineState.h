#pragma once

#include <type_traits>

/* 
 * NOTE: CURRNTLY ERR IS NOT HANDLED RIGHT -- SHOULD HAVE A STACK I THINK?
 */

// Remove n from the stack
void popn(Program& s, size_t n) {
	for(size_t i=0;i<n;i++) s.pop();
}

// Check if a variadic template Ts contains any type X
template<typename X, typename... Ts>
constexpr bool contains_type() {
	return std::disjunction<std::is_same<X, Ts>...>::value;
}

template<typename t_x, typename t_return>
class VirtualMachineState {
	
	// This stores the state of an evaluator of a node. The idea here is that we want to be able to encapsulate everything
	// about the evaluation of a tree so that we can stop it in the middle and resume later, as is required for stochastics
	// This must be automatically generated because it depends on the types in the grammar. 
public:
	
	static const unsigned long MAX_RECURSE = 64;
	static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
	Program            opstack; 
	Stack<t_x>         xstack; //xstackthis stores a stack of the x values (for recursive calls)
	t_return           err; // what error output do we return?
	double             lp; // the probability of this context
	
	unsigned long 	  recursion_depth; // when I was created, what was my depth?

	// This is a little bit of fancy template metaprogramming that allows us to define a stack
	// like std::tuple<Stack<bool>, Stack<std::string> > using a list of type names defined in NT_TYPES
	template<typename... args>
	struct t_stack { std::tuple<Stack<args>...> value; };
	t_stack<NT_TYPES> stack; // our stacks of different types
	
	typedef int index_t; // how we index into factorized lexica -- NOTE: probably should be castable from Instruction.arg 
	
	// must have a memoized return value, that permits factorized by requiring an index argument
	std::map<std::pair<index_t, t_x>, t_return> mem; 

	// when we recurse and memoize, this stores the arguments (index and t_x) for us to 
	// rember after the program trace is done
	Stack<std::pair<index_t, t_x>> memstack;

	abort_t aborted; // have we aborted?
	
	VirtualMachineState(t_x x, t_return e, size_t _recursion_depth=0) :
		err(e), lp(0.0), recursion_depth(_recursion_depth), aborted(abort_t::NO_ABORT) {
		xstack.push(x);
	}
	
	virtual ~VirtualMachineState() {};	// needed so VirtualMachinePool can delete
	
	bool operator<(const VirtualMachineState& m) const {
		/* These must be sortable by lp so that we can enumerate them from low to high probability in a VirtualMachinePool 
		 * NOTE: VirtualMachineStates shouldn't be put in a set because they might evaluate to equal! */
		return lp < m.lp; //(-lp) < (-m.lp);
	}
	
	void increment_lp(double v) {
		lp += v;
	}
	
	template<typename T>
	T getpop() {
		// retrieves and pops the element of type T from the stack
		//if(aborted != abort_t::NO_ABORT) return T(); // don't try to access the stack because we're aborting
		assert(std::get<Stack<T>>(stack.value).size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		T x = std::move(std::get<Stack<T>>(stack.value).top());
		std::get<Stack<T>>(stack.value).pop();
		return x;
	}
	template<typename T>
	T gettop() {
		// retrieves but does not remove
		//if(aborted != abort_t::NO_ABORT) return T(); // don't try to access the stack because we're aborting
		assert(std::get<Stack<T>>(stack.value).size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		return std::get<Stack<T>>(stack.value).top();
	}
	
	template<typename T>
	auto top() {
		// get a reference to the top. This is used by a major 
		return std::get<Stack<T>>(stack.value).top();
	}
	
	template<typename T>
	bool empty() {
		return std::get<Stack<T>>(stack.value).empty();
	}

	template<typename T>
	void push(T x){
		// push things onto the appropriate stack
		std::get<Stack<T>>(stack.value).push(x);
	}	
		

	template<typename... args>
	bool _stacks_empty() const { 
		return (... && std::get<Stack<args>>(stack.value).empty()); 
	}
	
	bool stacks_empty() const { 
		// return strue if stack is empty -- as a check at the end of a evaluation
		return this->_stacks_empty<NT_TYPES>();
		
	}
	
	virtual t_return run(Dispatchable<t_x,t_return>* d) {
		// defaulty run on a null pool with same dispatch and recurse handler
		return run(nullptr, d, d);
	}
	
	virtual t_return run(VirtualMachinePool<t_x, t_return>* pool, Dispatchable<t_x,t_return>* dispatch, Dispatchable<t_x,t_return>* loader) {
		// Run with a pointer back to pool p. This is required because "flip" may push things onto the pool.
		// Here, dispatch is called to evaluate the function, and loader is called on recursion (allowing us to handle recursion
		// via a lexicon or just via a LOTHypothesis). 

		aborted = abort_t::NO_ABORT;

		while(!opstack.empty()){
			if(aborted != abort_t::NO_ABORT) return err;
			FleetStatistics::vm_ops++;
			
			Instruction i = opstack.top(); opstack.pop();

			if(i.is_custom()) {
				abort_t b = dispatch->dispatch_rule(i, pool, *this, loader);
				if(b != abort_t::NO_ABORT) {
					aborted = b;
					return err;
				}
			}
			else {
				
				switch(i.getBuiltin()) {
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
					case BuiltinOp::op_MEM:
					{
						auto v = gettop<t_return>(); // what I should memoize should be on top here, but dont' remove because we also return it
						auto memindex = memstack.top(); memstack.pop();
						if(mem.count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrte if you have
							mem[memindex] = v;
						}
						
						break;
					}
					case BuiltinOp::op_TRUE: 
					{
						if constexpr (contains_type<bool,NT_TYPES>()) { 
							push<bool>(true);
						} else { assert(0 && "*** Must have bool defined to use op_TRUE");}
						break;
					}
					case BuiltinOp::op_FALSE: 
					{
						if constexpr (contains_type<bool,NT_TYPES>()) { 
							push<bool>(false);
						} else { assert(0 && "*** Must have bool defined to use op_FALSE");}
						break;
					}
					case BuiltinOp::op_SAFE_RECURSE: {
						// This is a recursion that returns empty for empty arguments 
						// simplifying the condition
						if constexpr (std::is_same<t_x, std::string>::value) { 						

							// need to check if stack<t_x> is empty since thats where we get x
							if(empty<t_x>() or top<t_x>().size() == 0) { 
								getpop<t_x>(); // this would have been the argument
								push<t_return>(t_return{}); //push default (null) return
								continue;
							}
						} else { assert(false && "*** Can only use SAFE_MEM_RECURSE on strings");}
						
						// want to fallthrough here
						[[fallthrough]];
					}
					case BuiltinOp::op_RECURSE:
					{
						
						if(recursion_depth++ > MAX_RECURSE) { // there is one of these for each recurse
							aborted = abort_t::RECURSION_DEPTH;
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
						if constexpr (std::is_same<t_x, std::string>::value) { 						
							if(empty<t_x>() or top<t_x>().size() == 0) { 
								getpop<t_x>(); // this would have been the argument
								push<t_return>(t_return{}); //push default (null) return
								continue;
							}
						} else { assert(false && "*** Can only use SAFE_MEM_RECURSE on strings");}

						// want to fallthrough here
						[[fallthrough]];
					}
					case BuiltinOp::op_MEM_RECURSE:
					{
						
						if(recursion_depth++ > MAX_RECURSE) {
							aborted = abort_t::RECURSION_DEPTH;
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
						if constexpr (contains_type<bool,NT_TYPES>()) { 
							assert(pool != nullptr && "op_FLIP and op_FLIPP require the pool to be non-null, since they push onto the pool"); // can't do that, for sure
					
							double p = 0.5; 
							
							if constexpr (contains_type<double,NT_TYPES>()) {  // if we have double allowed we cna do this
								if(i.getBuiltin() == BuiltinOp::op_FLIPP) { // only for built-in ops do we 
									p = getpop<double>(); // reads a double argfor the coin weight
									if(std::isnan(p)) { p = 0.0; } // treat nans as 0s
									assert(p <= 1.0 && p >= 0.0);
								}
							} else { assert(i.getBuiltin() != BuiltinOp::op_FLIPP); } // tif there is no double, we can't use flipp
							
				
							pool->copy_increment_push(this, true,  log(p));
//							pool->copy_increment_push(*this, false,  log(1.0-p));
							bool b = pool->increment_push(this, false, log(1.0-p)); 
							
							// TODO: This is clumsy, ugly mechanism -- need to re-do
							
							// since we pushed this back onto the queue (via increment_push), we need to tell the 
							// pool not to delete this, so we send back this special signal
							if(b) { // theoutcome of increment_push decides whether I am deleted or not
								aborted = abort_t::RANDOM_CHOICE_NO_DELETE; 
							}
							else {
								aborted = abort_t::RANDOM_CHOICE; 
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
//								aborted = abort_t::RANDOM_CHOICE;
//							}

							break;
	
						} else { assert(0 && "*** Cannot use op_FLIP without defining bool in NT_TYPES"); }
					}
					case BuiltinOp::op_JMP:
					{
						popn(opstack, i.arg);
						break;
					}
					case BuiltinOp::op_IF: 
					{
						if constexpr (contains_type<bool,NT_TYPES>()) { 
							// Here we evaluate op_IF, which has to short circuit and skip (pop some of the stack) 
							bool b = getpop<bool>(); // bool has already evaluted
							
							// now ops must skip the xbranch
							if(!b) popn(opstack, i.arg);
							else   {}; // do nothing, we pass through and then get to the jump we placed at the end of the x branch
							
							break;		
						} else { assert(0 && "*** Cannot use op_IF without defining bool in NT_TYPES"); }			
					}
					default: 
					{
						// otherwise call my dispatch and return if there is an error 
						assert(0 && "Bad op name");
					}
				} // end switch
			
			} // end if not custom
		} // end loop over ops
	
		if(aborted != abort_t::NO_ABORT) 
			return err;		
	
		auto ret = getpop<t_return>();
		
		assert(stacks_empty() and xstack.size() == 1 && "When we return, all of the stacks should be empty or else something is awry.");
		
		return ret; 
	}	
	
};
