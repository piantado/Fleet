#pragma once

#include <type_traits>

/* 
 * 
 * We should rewrite with an "index" stack, and an index type (e.g. short). 
 * That would prevent us from having to use "short"
 * and it will make memoization easier -- make mem a tuple
 * 
 * We can probably avoid doing nt_bool if we use an #ifdef statement to check if we have
 * defined op_FLIP?
 * 
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
//	static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
	Program            opstack; 
	std::stack<t_x>    xstack; //xstackthis stores a stack of the x values (for recursive calls)
	t_return           err; // what error output do we return?
	double             lp; // the probability of this context
	
	unsigned long 	  recursion_depth; // when I was created, what was my depth?

	// This is a little bit of fancy template metaprogramming that allows us to define a stack
	// like std::tuple<std::stack<bool>, std::stack<std::string> > using a list of type names defined in NT_TYPES
	template<typename... args>
	struct t_stack { std::tuple<std::stack<args>...> value; };
	t_stack<NT_TYPES> stack; // our stacks of different types
		
	// must have a memoized return value, that permits factorized by requiring an index argument
	std::map<std::pair<short, t_x>, t_return> mem; 

	// when we recurse and memoize, this stores the arguments (index and t_x) for us to 
	// rember after the program trace is done
	std::stack<std::pair<short, t_x>> memstack;

	abort_t aborted; // have we aborted?
	
	VirtualMachineState(t_x x, t_return e, size_t _recursion_depth=0) :
		err(e), lp(0.0), recursion_depth(_recursion_depth), aborted(NO_ABORT) {
		xstack.push(x);
	}
	
	virtual ~VirtualMachineState() { }
	
//	VirtualMachineState(const VirtualMachineState& vms) // copy constructor automatically defined
	
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
		if(aborted) return T(); // don't try to access the stack because we're aborting
		assert(std::get<std::stack<T>>(stack.value).size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		T x = std::get<std::stack<T>>(stack.value).top();
		std::get<std::stack<T>>(stack.value).pop();
		return x;
	}
	template<typename T>
	T gettop() {
		// retrieves but does not remove
		if(aborted) return T(); // don't try to access the stack because we're aborting
		assert(std::get<std::stack<T>>(stack.value).size() > 0 && "Cannot pop from an empty stack -- this should not happen! Something is likely wrong with your grammar's argument types, return type, or arities.");
		
		return std::get<std::stack<T>>(stack.value).top();
	}
	
	template<typename T>
	void push(T x){
		// push things onto the appropriate stack
		std::get<std::stack<T>>(stack.value).push(x);
	}	
	
	template<typename T>
	size_t stacksize() {
		return std::get<std::stack<T>>(stack.value).size();
	}
	
	
	virtual t_return run(Dispatchable<t_x,t_return>* d) {
		// defaulty run on a null pool with same dispatch and recurse handler
		return run(nullptr, d, d);
	}
	
	virtual t_return run(VirtualMachinePool<t_x, t_return>* pool, Dispatchable<t_x,t_return>* dispatch, Dispatchable<t_x,t_return>* loader) {
		// Run with a pointer back to pool p. This is required because "flip" may push things onto the pool.
		// Here, dispatch is called to evaluate the function, and loader is called on recursion (allowing us to handle recursion
		// via a lexicon or just via a LOTHypothesis). 
		
		while(!opstack.empty()){
			if(aborted) return err;
			FleetStatistics::vm_ops++;
			
			Instruction i = opstack.top(); opstack.pop();

			if(i.is_custom) {
				abort_t b = dispatch->dispatch_rule(i, pool, this, loader);
				if(b != NO_ABORT) {
					aborted = b;
					return err;
				}
			}
			else {
				
				switch(i.builtin) {
					case op_NOP: 
					{
						break;
					}
					case op_X:
					{
						push<t_x>(xstack.top());
						break;
					}
					case op_POPX:
					{
						xstack.pop(); // NOTE: Remember NOT to pop from getpop<t_x>() since that's not where x is stored
						break;
					}
					case op_MEM:
					{
						auto v = gettop<t_return>(); // what I should memoize should be on top here, but dont' remove because we also return it
						auto memindex = memstack.top(); memstack.pop();
						if(mem.count(memindex)==0) { // you might actually have already placed mem in crazy recursive situations, so don't overwrte if you have
							mem[memindex] = v;
						}
					}
					case op_RECURSE:
					{
						
						if(recursion_depth++ > MAX_RECURSE) { // there is one of these for each recurse
							aborted = RECURSION_DEPTH;
							return err;
						}
						
						// if we get here, then we have processed our arguments and they are stored in the t_x stack. 
						// so we must move them to the x stack (where there are accessible by op_X)
						xstack.push(getpop<t_x>());
						opstack.push(Instruction(op_POPX)); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
						
						loader->push_program(opstack,0); // push this program 
						
						// after execution is done, the result will be pushed onto t_return
						// which is what gets returned when we are all done
						
						break;
					}
					case op_RECURSE_FACTORIZED:
					{
						// the index of who we recurse to is assumed stored in arg
						if(recursion_depth++ > MAX_RECURSE) {
							aborted = RECURSION_DEPTH;
							return err;
						}
						
						xstack.push(getpop<t_x>());							
						opstack.push(Instruction(op_POPX));
						
						loader->push_program(opstack,i.arg);
						
						break;
					}
					case op_FLIP: 
					{
						// We're going to duplicate code a little bit so that we 
						// don't need to have double defined for FLIP (e.g. p=0.5 always)
						if constexpr (contains_type<bool,NT_TYPES>()) { 
						
							double p = 0.5; 

							VirtualMachineState<t_x,t_return>* v0 = new VirtualMachineState<t_x,t_return>(*this);
							v0->increment_lp(log(1.0-p));
							v0->push<bool>(false); // add this value to the stack since we make this choice
							pool->push(v0);					
													
							VirtualMachineState<t_x,t_return>* v1 = new VirtualMachineState<t_x,t_return>(*this);
							v1->increment_lp(log(p));
							v1->push<bool>(true); // add this value to the stack since we make this choice
							pool->push(v1);	
							
							aborted = RANDOM_CHOICE; // this context is aborted please 
							return err;
		
						// and fall through
						} else { assert(0 && "*** Cannot use op_FLIP without defining bool in NT_TYPES"); }
					}
					case op_FLIPP:
					{
						if constexpr (contains_type<bool,NT_TYPES>() && contains_type<double,NT_TYPES>() ) { 
						assert(pool != nullptr && "op_FLIP and op_FLIPP require the pool to be non-null, since they push onto the pool"); // can't do that, for sure
					
						double p = getpop<double>(); // reads a double argfor the coin weight
						if(std::isnan(p)) { p = 0.0; } // treat nans as 0s
						assert(p <= 1.0 && p >= 0.0);
						
						VirtualMachineState<t_x,t_return>* v0 = new VirtualMachineState<t_x,t_return>(*this);
						v0->increment_lp(log(1.0-p));
						v0->push<bool>(false); // add this value to the stack since we make this choice
						pool->push(v0);					
												
						VirtualMachineState<t_x,t_return>* v1 = new VirtualMachineState<t_x,t_return>(*this);
						v1->increment_lp(log(p));
						v1->push<bool>(true); // add this value to the stack since we make this choice
						pool->push(v1);	
						
						aborted = RANDOM_CHOICE; // this context is aborted please 
						return err;
						/*
						
						// In this implementation, we keep going as long as the lp doesn't drop too
						// low, always assuming if evaluates to true
						// and then how we follow the true branch
						increment_lp(log(p));
						push<bool>(true); // add this value to the stack since we make this choice
							
						// now if we can't just keep going, copy myself and put me on the stack
						if(lp < pool->Q.top()->lp - LP_BREAKOUT) { // TODO: INCLUDE THE CONTEXT LIMIT HERE 
							pool->push(new VirtualMachineState<t_x,t_return>(*this));
							
							aborted = RANDOM_CHOICE; // this context is aborted please 
							
							return err;
						}
						// else continue -- keep going now that true is pushed
						*/

						break;
						} else { assert(0 && "*** Cannot use op_FLIPP without defining bool and double in NT_TYPES"); }
					}

					case op_JMP:
					{
						popn(opstack, i.arg);
						break;
					}
					case op_IF: 
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
	
		if(aborted) return err;		
	
		return getpop<t_return>(); 
	}	
	
};
