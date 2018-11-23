#ifndef VIRTUAL_MACHINE_STATE
#define VIRTUAL_MACHINE_STATE

// NOTE: CURRNTLY ERR IS NOT HANDLED RIGHT -- SHOULD HAVE A STACK I THINK?



/* 
 * 
 * We should rewrite with an "index" stack, and an index type (e.g. short). 
 * That would prevent us from having to use "short"
 * and it will make memoization easier -- make mem a tuple
 * 
 * We can probably avoid doing nt_bool if we use an IF statement to check if we have
 * defined op_FLIP?
 * 
 */


unsigned long global_vm_ops = 0;

// Remove n from the stack
void popn(Opstack& s, size_t n) {
	for(size_t i=0;i<n;i++) s.pop();
}


template<typename t_x, typename t_return>
class VirtualMachineState {
	
	// This stores the state of an evaluator of a node. The idea here is that we want to be able to encapsulate everything
	// about the evaluation of a tree so that we can stop it in the middle and resume later, as is required for stochastics
	// This must be automatically generated because it depends on the types in the grammar. 
	
	static const unsigned long MAX_RECURSE = 64;
	static constexpr double    LP_BREAKOUT = 5.0; // we keep executing a probabilistic thread as long as it doesn't cost us more than this compared to the top
	
public:
	Opstack            opstack; 
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

	t_abort aborted; // have we aborted?
	
	VirtualMachineState(t_x x, t_return e, size_t _recursion_depth=0) :
		err(e), lp(0.0), recursion_depth(_recursion_depth), aborted(NO_ABORT) {
		xstack.push(x);
	}
	
	VirtualMachineState(const VirtualMachineState& vms) : 
		opstack(vms.opstack), xstack(vms.xstack), err(vms.err), lp(vms.lp), 
		recursion_depth(vms.recursion_depth), stack(vms.stack), mem(vms.mem), aborted(vms.aborted) {
			
		assert(opstack.size() == vms.opstack.size());
		assert(xstack.size() == vms.xstack.size());
		assert(mem.size() == vms.mem.size());
	}
	
	virtual ~VirtualMachineState() {
		
	}
	
	bool operator<(const VirtualMachineState& m) const {
		/* These must be sortable by lp so that we can enumerate them from low to high probability in a VirtualMachinePool 
		 * NOTE: These shouldn't be put in a set because they might evaluate to equal! */
		return lp < m.lp;
	}
	
	void increment_lp(double v) {
		lp += v;
	}
	
	template<typename T>
	T getpop() {
		// retrieves and pops the element of type T from the stack
		if(aborted) return T(); // don't try to access the stack because we're aborting
		assert(std::get<std::stack<T>>(stack.value).size() > 0 && "Cannot pop from an empty stack -- this should not happen!");
		
		T x = std::get<std::stack<T>>(stack.value).top();
		std::get<std::stack<T>>(stack.value).pop();
		return x;
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
			
			op_t op = opstack.top(); opstack.pop();
			++global_vm_ops;

			//CERR "OP = " << op ENDL;
			
			switch(op) {
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
					// by definition, this peeks at the top of t_x and t_return and caches
//					mem[xstack.top()] = std::get<std::stack<t_return>>(stack.value).top();
					
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
					opstack.push(op_POPX); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
					
					loader->push_program(opstack,0); // push this program 
					
					// after execution is done, the result will be pushed onto t_return
					// which is what gets returned when we are all done
					
					break;
				}
				case op_RECURSE_FACTORIZED:
				{
					if(recursion_depth++ > MAX_RECURSE) {
						aborted = RECURSION_DEPTH;
						return err;
					}
					short idx = getpop<short>(); // shorts are *always* used to index into factorized lexica
					
					xstack.push(getpop<t_x>());							
					opstack.push(op_POPX); 
					
					loader->push_program(opstack,idx);
					
					break;
				}
//				case op_MEM_RECURSE:
//				{
//					if(recursion_depth++ > MAX_RECURSE) { // there is one of these for each recurse
//						aborted = RECURSION_DEPTH;
//						return err;
//					}
//					
//					// if we get here, then we have processed our arguments and they are stored in the t_x stack. 
//					// so we must move them to the x stack (where there are accessible by op_X)
//					auto m = std::make_pair(0, getpop<t_x>());
//					
//					if(mem.count(m)) {  // if we have done this already
//						push<t_return>(mem[m]); // return this value
//					}
//					else { // we have to evaluate 
// 						
//						xstack.push(getpop<t_x>());
//
//						opstack.push(op_POPX); // we have to remember to remove X once the other program evaluates, *after* everything has evaluated
//						opstack.push(op_MEM); // mem looks at the top of the stack and caches the current x value
//						push<short>(0); // we used index zero since non-factorized 
//						loader->push_program(opstack,0); // push this program 
//					}
//					
//					break;
//				}
//				case op_MEM_RECURSE_FACTORIZED:
//				{
//					if(recursion_depth++ > MAX_RECURSE) {
//						aborted = RECURSION_DEPTH;
//						return err;
//					}
//					short idx = getpop<short>(); // shorts are *always* used to index into factorized lexica
//					
//					auto m = std::make_pair(0, getpop<t_x>());
//					
//					if(mem.count(m)) {  // if we have done this already
//						push<t_return>(mem[m]); // return this value
//					}
//					else {
//					
//						xstack.push(getpop<t_x>());							
//						opstack.push(op_POPX); 
//						
//						loader->push_program(opstack,idx);
//						continue;
//					}
//					
//					break;
//				}
//				case op_MEM_RECURSE_FACTORIZED:
//				{
//					if(recursion_depth++ > MAX_RECURSE) {
//						aborted = RECURSION_DEPTH;
//						return err;
//					}
//					short idx = getpop<short>(); // shorts are *always* used to index into factorized lexica
//					
//					xstack.push(getpop<t_x>());							
//					opstack.push(op_POPX); 
//					
//					loader->push_program(opstack,idx);
//					continue;
//
//					break;
//				}
//				case op_MEM: 
//				{
//					// need to get x and return value off stack
//					break;
//				}
				case op_FLIP: 
				{
					// flip with default 0.5 arg
					push<double>(0.5);
					
					// and fall through
				}
				//[[fallthrough]];  // hmm I thought I needed this, to mark fallthroughs, but it gives me a warning!
				case op_FLIPP:
				{
					
					
					assert(pool != nullptr && "op_FLIP and op_FLIPP require the pool to be non-null, since they push onto the pool"); // can't do that, for sure
				
					double p = getpop<double>(); // reads a double argfor the coin weight
					if(isnan(p)) { p = 0.0; } // treat nans as 0s
					assert(p <= 1.0 && p >= 0.0);
					
					// In this implementation, we keep going as long as the lp doesn't drop too
					// low, always assuming if evaluates to true
					
					VirtualMachineState<t_x,t_return>* v0 = new VirtualMachineState<t_x,t_return>(*this);
					v0->increment_lp(log(1.0-p));
					v0->push<bool>(false); // add this value to the stack since we make this choice
					pool->push(v0);					
					
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

					break;
					
				}
				case op_JMP:
				{
					// jump relative by taking the next item off the instruction pointer and treating it as an integer offset
					// NOTE: This is not the smartest thing to do since it requires storing an integer offset as a op code, but
					// it should be fine
					// NOTE: We have not implemented negative jumps yet -- because we are using a stack. 
					// but that's okay because all that is needed is positive to implement IF
					
					int r = (int)opstack.top(); // not safe or wonderful but should be okay to use ops to encode jump sizes
					opstack.pop(); // because we just removed (then cast) one for r
					
					// now remove as many as r told you to
					popn(opstack, r);
					
					break;
				}
				case op_IF: 
				{
					// Here we evaluate op_IF, which has to short circuit and skip (pop some of the stack) 
					bool b = getpop<bool>(); // bool has already evaluted
					
					int xsize = (int) opstack.top(); opstack.pop();
					
					if(!b) {  // now ops must skip the xbranch
						popn(opstack, xsize);
					}
					else {
						// do nothing, we pass through and then get to the jump we placed at the end of the x branch
					}
					
					break;					
				}
				default: 
				{
					// otherwise call my dispatch and return if there is an error 
					t_abort b = dispatch->dispatch_rule(op, pool, this);
					if(b != NO_ABORT) {
						aborted = b;
						return err;
					}
				}
			} // end switch
			
		} // end loop over ops
	
		if(aborted) return err;		
	
		return getpop<t_return>(); 
	}	
	
};

#endif