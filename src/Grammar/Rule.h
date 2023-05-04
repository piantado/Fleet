#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Instruction.h"
#include "Nonterminal.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "IO.h"

 /**
  * @class Rule
  * @author piantado
  * @date 08/02/20
  * @file Rule.h
  * @brief A Rule stores one possible expansion in the grammar, specifying a nonterminal type, an instruction that gets executed, a forma string, a number of children, and an array of types of each child. 
  *  Here we "emulate" a type system using t_nonterminal to store an integer for the types.   * 
  */ 
class Rule {

public:
	static const std::string ChildStr; // how do strings get substituted?
	static const std::string SilentChildStr; // these are arguments to the function but not substituted in when displaying

	nonterminal_t                      nt;
	std::string                        format; // how am I printed?
	size_t                             N; // how many children?
	double                             p;
	
	// store this information for creating instructions
	void*					   fptr;
	Op 						   op; // for ops that need names
	int 					   arg=0;
	std::vector<nonterminal_t> child_types; // An array of what I expand to; note that this should be const but isn't to allow list initialization (https://stackoverflow.com/questions/5549524/how-do-i-initialize-a-member-array-with-an-initializer-list)

protected:
	
	std::size_t          my_hash; // a hash value for this rule
	
public:
	// Rule's constructors convert CustomOp and BuiltinOp to the appropriate instruction types
	Rule(const nonterminal_t rt, 
	     void* f, 
		 const std::string fmt, 
		 std::initializer_list<nonterminal_t> c, 
		 double _p=1.0, 
		 Op o=Op::Standard, 
		 int a=0) :
		nt(rt), format(fmt), N(c.size()), p(_p), fptr(f),  op(o), arg(a), child_types(c) {
			
		// Set up hashing for rules (cached so we only do it once)
		// NOTE: We do NOT want to hash anything about the function pointer, since then our ordering
		// will change (and thus our runs will change) from run to run. So we don't include that. 
		my_hash = 1;
		hash_combine(my_hash, (size_t)nt);
		
		for(size_t k=0;k<N;k++) 
			hash_combine(my_hash, (size_t)op, (size_t)child_types[k], (size_t) arg);
		
		for(auto& thechar : format) 
			hash_combine(my_hash, (size_t)thechar );
			
		// check that the format string has the right number of %s
		// NOTE: here we ONLY check "%", not "%s" since we now allow %1, %2, %3, etc. 
		assert(ChildStr.substr(0,1) == SilentChildStr.substr(0,1)); // this must be true ya know for the next line to be right
		if(N != count(fmt, ChildStr.substr(0,1))) {
			CERR "*** Wrong number of format string arguments in " << fmt ENDL;
			assert(false);
		}
	}
	
	bool is_a(Op o) const {
		return o == op;
	}
	
	bool is_recursive() const {
		return is_a(Op::Recurse) or 
			   is_a(Op::MemRecurse) or 
			   is_a(Op::SafeRecurse) or 
			   is_a(Op::SafeMemRecurse) or 
			   is_a(Op::LexiconRecurse) or 
			   is_a(Op::LexiconMemRecurse) or 
			   is_a(Op::LexiconSafeRecurse) or 
			   is_a(Op::LexiconSafeMemRecurse);
	}
	
	// Two versions: one for when we have a specified, and one for where we read from arg
	// NOTE: These can't be defauled into templates because in one case we use a data member
	// and in the other we specify what a should be 
	Instruction makeInstruction(int a) const {
		assert(arg==0 && "*** You have specified a when arg != 0 -- this is probably a mistake.");
		return Instruction(fptr, a);
	}
	
	Instruction makeInstruction() const {
		return Instruction(fptr, arg);
	}
	
	
	auto operator<=>(const Rule& r) const {
		/**
		 * @brief We sort rules so that they can be stored in arrays in a standard order. For enumeration, it's actually important that we sort them with the terminals first. 
		 * 		  So we sort first by terminals and then by probability (higher first). 
		 * @param r
		 * @return 
		 */
		
		// This is structured so that we always put terminals first and then we put the HIGHER probability things first. 
		// this helps in enumeration
		if( (N==0) != (r.N==0) ) 
			return (r.N==0) <=> (N==0);
		else if(p != r.p) {
			// We sort in DECREASING order of prob, so this is needed here
			// NOTE: we can't use spaceship because of nan
			return fp_ordering(r.p, p);
		}
		else if(format != r.format) // if probabilities and children are equal, set a fixed order based on hash
			return format <=> r.format;
		else
			return my_hash <=> r.my_hash;
	}
	
	bool operator==(const Rule& r) const {
		/**
		 * @brief Two rules are equal if they have the same instructions, nonterminal, format, children, and children types
		 * @param r
		 * @return 
		 */
		
		if(not (nt==r.nt and fptr == r.fptr and op ==r.op and format==r.format and N==r.N and p==r.p)) return false;
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
		std::string out = "[RULE " + format + ", ";
		for(size_t i =0;i<N;i++) {
			out += str(child_types[i]) + "x";
		}
		if(N > 0) out = out.erase(out.size()-1); // remove last x
		out += "->" + str(nt) + ", p\u221D" + str(p) + "]";
		return out;
	}
	
};

std::ostream& operator<<(std::ostream& o, const Rule& r) {
	o << r.string();
	return o;
}

// A single constant NullRule for gaps in trees. Always has type 0
// old format was \u2b1c 
const Rule* NullRule = new Rule((nonterminal_t)0, nullptr, "\u25A0", {}, 0.0);

const std::string Rule::ChildStr = "%s";
const std::string Rule::SilentChildStr = "%!";