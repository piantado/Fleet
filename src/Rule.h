#pragma once

#include <functional>
#include <string>

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
protected:
	std::size_t          my_hash; // a hash value for this rule
public:
	// Rule's constructors convert CustomOp and BuiltinOp to the appropriate instruction types
	Rule(const nonterminal_t rt, const CustomOp o, const std::string fmt, std::initializer_list<nonterminal_t> c, double _p, const int arg=0) :
		nt(rt), instr(o,arg), format(fmt), N(c.size()), p(_p) {
		// mainly we just convert c to an array
		child_types = new nonterminal_t[c.size()];
		std::copy(c.begin(), c.end(), child_types);
		
		// Set up hashing for rules (cached so we only do it once)
		std::hash<std::string> h; 
		my_hash = h(fmt);
		hash_combine(my_hash, (size_t) o, (size_t) arg, (size_t)nt);
		for(size_t i=0;i<N;i++) 
			hash_combine(my_hash, i, (size_t)child_types[i]);
		
		
		// check that the format string has the right number of %s
		assert( N == count(fmt, ChildStr) && "*** Wrong number of format string arguments");
	}
		
	Rule(const nonterminal_t rt, const BuiltinOp o, const std::string fmt, std::initializer_list<nonterminal_t> c, double _p, const int arg=0) :
		nt(rt), instr(o,arg), format(fmt), N(c.size()), p(_p) {
		
		// mainly we just convert c to an array
		child_types = new nonterminal_t[c.size()];
		std::copy(c.begin(), c.end(), child_types);
		
		// Set up hashing for rules (cached so we only do it once)
		std::hash<std::string> h; 
		my_hash = h(fmt);
		hash_combine(my_hash, (size_t) o);
		hash_combine(my_hash, (size_t) arg);
		hash_combine(my_hash, (size_t) nt);
		for(size_t i=0;i<N;i++) hash_combine(my_hash, (size_t)child_types[i]);
		
		// check that the format string has the right number of %s
		assert( N == count(fmt, ChildStr) && "*** Wrong number of format string arguments");
	}
	

	~Rule() {
		delete[] child_types;
	}
	Rule(const Rule& r) = delete;  // our code should not be using this
	Rule(Rule&& r) = delete; 
	void operator=(const Rule& r) = delete; 
	void operator=( Rule&& r) = delete; 
	
	bool operator<(const Rule& r) const {
		// This is structured so that we always put terminals first and then we put the HIGHER probability things first. 
		// this helps in enumeration
		if(N < r.N) return true;
		else		return p > r.p; // weird, but helpful, that we sort in decreasing order of probability
	}
	bool operator==(const Rule& r) const {
		if(not (nt==r.nt and instr==r.instr and format==r.format and N==r.N and p==r.p)) return false;
		for(size_t i=0;i<N;i++) {
			if(child_types[i] != r.child_types[i]) return false;
		}
		return true;
	}
	
	size_t get_hash() const {
		return my_hash;
	}
	
	bool is_terminal() const {
		// I am a terminal rule if I have no children
		return N==0;
	}
	
//	size_t count_children_of_type(const nonterminal_t nt) const {
//		size_t n=0;
//		for(size_t i=0;i<N;i++){
//			n += (child_types[i] == nt);
//		}
//		return n;
//	}
//	
//	size_t replicating_children() const { // how many children are of  my type?
//		return count_children_of_type(nt);
//	}
	
//	size_t random_replicating_index() const {
//		// return the index of something replicating
//		size_t R = replicating_children();
//		size_t r = myrandom(R);
//		for(size_t i=0;i<N;i++) {
//			if(child_types[i] == nt) { // if I'm replicating
//				if(r==0) return i;
//				else     --r;
//			}
//		}
//		assert(false && "You tried to sample a replicating index when there were none!");
//	}
	
};


// A single constant NullRule for gaps in trees. Always has type 0
const Rule* NullRule = new Rule((nonterminal_t)0, BuiltinOp::op_NOP, "\u2b1c", {}, 0.0);
 