#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Instruction.h"
#include "Nonterminal.h"
#include "Miscellaneous.h"
#include "Strings.h"

/* 
 * 
 * */
 
 /**
  * @class Rule
  * @author piantado
  * @date 08/02/20
  * @file Rule.h
  * @brief A Rule stores one possible expansion in the grammar, specifying a nonterminal type, an instruction that gets executed, a forma string, a number of children, and an array of types of each child. 
  *  Here we "emulate" a type system using t_nonterminal to store an integer for the types.   * 
  */ 
template<typename VirtualMachineState_t>
class Rule {

public:
	static const std::string ChildStr; // how do strings get substituted?

	nonterminal_t                      nt;
	Instruction<VirtualMachineState_t> instr; // a template for my instruction, which here mainly stores my optype
	std::string                        format; // how am I printed?
	size_t                             N; // how many children?
	double                             p;
		
protected:
	std::vector<nonterminal_t> child_types; // An array of what I expand to; note that this should be const but isn't to allow list initialization (https://stackoverflow.com/questions/5549524/how-do-i-initialize-a-member-array-with-an-initializer-list)

	std::size_t          my_hash; // a hash value for this rule
	
public:
	// Rule's constructors convert CustomOp and BuiltinOp to the appropriate instruction types
	Rule(const nonterminal_t rt, 
	     const Instruction<VirtualMachineState_t> i, 
		 const char* fmt, 
		 std::initializer_list<nonterminal_t> c, 
		 double _p) :
		nt(rt), instr(i), format(fmt), N(c.size()), p(_p), child_types(c) {
			
		// Set up hashing for rules (cached so we only do it once)
		std::hash<std::string> h; 
		my_hash = h(fmt);
		hash_combine(my_hash, (size_t)nt);
		for(size_t k=0;k<N;k++) 
			hash_combine(my_hash, reinterpret_cast<std::uintptr_t>(i.f), (size_t)child_types[k]);
		
		
		// check that the format string has the right number of %s
		assert( N == count(fmt, ChildStr) && "*** Wrong number of format string arguments");
	}
		
	bool operator<(const Rule& r) const {
		/**
		 * @brief We sort rules so that they can be stored in arrays in a standard order. For enumeration, it's actually important that we sort them with the terminals first. 
		 * 		  So we sort first by terminals and then by probability (higher first). 
		 * @param r
		 * @return 
		 */
		
		// This is structured so that we always put terminals first and then we put the HIGHER probability things first. 
		// this helps in enumeration
		if( (N==0) != (r.N==0) ) 
			return (r.N==0) < (N==0);
		else if(p != r.p) 
			return p > r.p; // weird, but helpful, that we sort in decreasing order of probability
		else  // if probabilities and children are equal, set a fixed order based on hash
			return my_hash < r.my_hash;
	}
	bool operator==(const Rule& r) const {
		/**
		 * @brief Two rules are equal if they have the same instructions, nonterminal, format, children, and children types
		 * @param r
		 * @return 
		 */
		
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
		/**
		 * @brief A terminal rule has no children. 
		 * @return 
		 */
		
		return N==0;
	}
	
	nonterminal_t type(size_t i) const {
		/**
		 * @brief What type is my i'th child?
		 * @param i
		 * @return 
		 */
		
		assert(i <= N && "*** Cannot get the type of something out of range");
		return child_types[i];
	}
	
	auto& get_child_types() const {
		return child_types;
	}
	
	std::string string() const {
		std::string out = "<" + str(nt) + " -> " + format + " : ";
		for(size_t i =0;i<N;i++) {
			out += " " + str(child_types[i]);
		}
		out += "\t w/ p \u221D " + str(p) + ">";
		return out;
	}
	
};

template<typename VirtualMachineState_t>
std::ostream& operator<<(std::ostream& o, const Rule<VirtualMachineState_t>& r) {
	o << r.string();
	return o;
}

// A single constant NullRule for gaps in trees. Always has type 0
// old format was \u2b1c 
template<typename VirtualMachineState_t>
const Rule<VirtualMachineState_t>* NullRule = new Rule<VirtualMachineState_t>((nonterminal_t)0, BuiltinOp::op_NOP, "\u25A0", {}, 0.0);

template<typename VirtualMachineState_t>
const std::string Rule<VirtualMachineState_t>::ChildStr = "%s";