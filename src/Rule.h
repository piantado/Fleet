#ifndef RULE_H
#define RULE_H

class Rule {
	// A rule stores nonterminal types and possible children
	// Here we "emulate" a type system using t_nonterminal to store an integer for the types
	// this means that the type only needs to be finally discovered in VirtualMachineState, since
	// everything else can operate on a uniform collection of rules. 
	
public:
	static size_t rule_id_counter; // shared across all rules -- used to determine equality,e tc. 

	const t_nonterminal  nonterminal_type;
	const op_t           op;
	const std::string    format; // how am I printed?
	const size_t          N; // how many children?
	t_nonterminal*       child_types; // An array of what I expand to; note that this should be const but isn't to allow list initialization (https://stackoverflow.com/questions/5549524/how-do-i-initialize-a-member-array-with-an-initializer-list)
	const double         p;
	const size_t         id;
		
	Rule(const t_nonterminal rt, const op_t o, const std::string fmt, const size_t n, t_nonterminal* c, const double _p) :
		nonterminal_type(rt), op(o), format(fmt), N(n), child_types(c), p(_p), id(rule_id_counter) {
		rule_id_counter++;
	}
	
	Rule(const t_nonterminal rt, const op_t o, const std::string fmt, const size_t n, std::initializer_list<t_nonterminal> c, const double _p) :
		nonterminal_type(rt), op(o), format(fmt), N(n), p(_p), id(rule_id_counter) {
		rule_id_counter++;
		
		// mainly we just convert c to an array
		child_types = new t_nonterminal[c.size()];
		std::copy(c.begin(), c.end(), child_types);
	}
	
};

size_t Rule::rule_id_counter = 0;

#endif