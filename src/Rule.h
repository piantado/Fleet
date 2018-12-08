#ifndef RULE_H
#define RULE_H

class Rule {
	// A rule stores nonterminal types and possible children
	// Here we "emulate" a type system using t_nonterminal to store an integer for the types
	// this means that the type only needs to be finally discovered in VirtualMachineState, since
	// everything else can operate on a uniforntrules. 
	
public:
	const nonterminal_t  nt;
	const Instruction    instr; // a template for my instruction, which here mainly stores my optype
	const std::string    format; // how am I printed?
	const size_t         N; // how many children?
	nonterminal_t*       child_types; // An array of what I expand to; note that this should be const but isn't to allow list initialization (https://stackoverflow.com/questions/5549524/how-do-i-initialize-a-member-array-with-an-initializer-list)
	const double         p;

	// Rule's constructors convert CustomOp and BuiltinOp to the appropriate instruction types
	Rule(const nonterminal_t rt, const CustomOp o, const std::string fmt, std::initializer_list<nonterminal_t> c, double _p, int arg=0) :
		nt(rt), instr(o,arg), format(fmt), N(c.size()), p(_p) {
		// mainly we just convert c to an array
		child_types = new nonterminal_t[c.size()];
		std::copy(c.begin(), c.end(), child_types);
	}
		
	Rule(const nonterminal_t rt, const BuiltinOp o, const std::string fmt, std::initializer_list<nonterminal_t> c, double _p, const int arg=0) :
		nt(rt), instr(o,arg), format(fmt), N(c.size()), p(_p) {
		
		// mainly we just convert c to an array
		child_types = new nonterminal_t[c.size()];
		std::copy(c.begin(), c.end(), child_types);
	}
	

	~Rule() {
		delete[] child_types;
	}
		
	size_t count_children_of_type(const nonterminal_t nt) const {
		size_t n=0;
		for(size_t i=0;i<N;i++){
			n += (child_types[i] == nt);
		}
		return n;
	}
	
	size_t replicating_children() const { // how many children are of  my type?
		return count_children_of_type(nt);
	}
	
	size_t random_replicating_index() const {
		// return the index of something replicating
		size_t R = replicating_children();
		size_t r = myrandom(R);
		for(size_t i=0;i<N;i++) {
			if(child_types[i] == nt) { // if I'm replicating
				if(r==0) return i;
				else     --r;
			}
		}
		assert(false && "You tried to sample a replicating index when there were none!");
	}
	
};

#endif