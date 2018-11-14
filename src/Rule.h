#ifndef RULE_H
#define RULE_H

class Rule {
	// A rule stores nonterminal types and possible children
	// Here we "emulate" a type system using t_nonterminal to store an integer for the types
	// this means that the type only needs to be finally discovered in VirtualMachineState, since
	// everything else can operate on a uniform collection of rules. 
	
public:
	const t_nonterminal  nonterminal_type;
	const op_t           op;
	const std::string    format; // how am I printed?
	const size_t         N; // how many children?
	t_nonterminal*       child_types; // An array of what I expand to; note that this should be const but isn't to allow list initialization (https://stackoverflow.com/questions/5549524/how-do-i-initialize-a-member-array-with-an-initializer-list)
	const double         p;
		
	Rule(const t_nonterminal rt, const op_t o, const std::string fmt, const size_t n, t_nonterminal* c, const double _p) :
		nonterminal_type(rt), op(o), format(fmt), N(n), child_types(c), p(_p) {
	}
	
	Rule(const t_nonterminal rt, const op_t o, const std::string fmt, const size_t n, std::initializer_list<t_nonterminal> c, const double _p) :
		nonterminal_type(rt), op(o), format(fmt), N(n), p(_p) {
		
		assert(c.size() == n); // if not, we are passing in the wrong number of arguments
		// mainly we just convert c to an array
		child_types = new t_nonterminal[c.size()];
		std::copy(c.begin(), c.end(), child_types);
	}
		
	size_t count_children_of_type(const t_nonterminal nt) const {
		size_t n=0;
		for(size_t i=0;i<N;i++){
			n += (child_types[i] == nt);
		}
		return n;
	}
	
	size_t replicating_children() const { // how many children are of  my type?
		return count_children_of_type(nonterminal_type);
	}
	
	size_t random_replicating_index() const {
		// return the index of something replicating
		size_t R = replicating_children();
		size_t r = myrandom(R);
		for(size_t i=0;i<N;i++) {
			if(child_types[i] == nonterminal_type) { // if I'm replicating
				if(r==0) return i;
				else     --r;
			}
		}
		assert(0);
	}
	
};

#endif