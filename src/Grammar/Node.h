#pragma once

#include <functional>
#include <stack>

#include "Hash.h"
#include "Rule.h"
#include "Program.h"
#include "BaseNode.h"

template<typename VirtualMachineState_t>
class Node : public BaseNode<Node<VirtualMachineState_t>> {
	friend class BaseNode<Node<VirtualMachineState_t>>;
	
public:

	// These are used in parseable to delimit nodes nonterminals and multiple nodes in a tree
	const static char NTDelimiter = ':'; // delimit nt:format 
	const static char RuleDelimiter = ';'; // delimit a sequence of nt:format;nt:format; etc

public:
	const Rule<VirtualMachineState_t>*  rule; // which rule did I use?
	double       lp; 
	bool         can_resample;	
	
	Node(const Rule<VirtualMachineState_t>* r=nullptr, double _lp=0.0, bool cr=true) : 
		BaseNode(r==nullptr?0:r->N), rule(r==nullptr ? NullRule<VirtualMachineState_t> : r), lp(_lp), can_resample(cr) {	
		// NOTE: We don't allow parent to be set here bcause that maeks pi hard to set. We shuold only be placed
		// in trees with set_child
	}
	
	/* We must define our own copy and move since parent can't just be simply copied */	
	Node(const Node<VirtualMachineState_t>& n) :
		BaseNode(n), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		this->fix_child_info();
	}
	Node(Node<VirtualMachineState_t>&& n) :
		BaseNode(n), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		this->children = std::move(n.children);
		this->fix_child_info();
	}
	
	virtual ~Node() {}
	
	
	nonterminal_t type(const size_t i) const {
		/**
		 * @brief Return the type of the i'th child
		 * @param i
		 * @return 
		 */
		
		return rule->type(i);
	}
	
	void set_child(size_t i, Node& n) {
		/**
		 * @brief Set my child to n. NOTE: This one needs to be used, rather than accessing children directly, because we have to set parent pointers and indices. 
		 * @param i
		 * @param n
		 */
		assert(i < rule->N);
		assert(n.nt() == rule->type(i));
		BaseNode<Node>::set_child(i,n);
	}
	void set_child(size_t i, Node&& n) {
		/**
		 * @brief Set my child to n. NOTE: This one needs to be used, rather than accessing children directly, because we have to set parent pointers and indices. 
		 * @param i
		 * @param n
		 */
		assert(i < rule->N);
		assert(n.nt() == rule->type(i));
		BaseNode<Node>::set_child(i,std::move(n));
	}


	void operator=(const Node& n) {
		// Here in the assignment operator we don't set the parent to n.parent, otherwise the parent pointers get broken
		// (nor pi). This leaves them in their place in a tree (so e.g. we can set a node of a tree and it still works)
		
		this->children = n.children;
		this->rule = n.rule;
		this->lp = n.lp;
		this->can_resample = n.can_resample;
		
		this->fix_child_info();
	}

	void operator=(Node&& n) {
		this->children = std::move(n.children);
		this->rule = n.rule;
		this->lp = n.lp;
		this->can_resample = n.can_resample;
		
		// NOTE we don't set parent here

		this->fix_child_info();
	}
	
	bool operator<(const Node& n) const {
		// We will sort based on the rules, except we recurse when they are unequal. 
		// This therefore sorts by the uppermost-leftmost rule that doesn't match. We are less than 
		// if we have fewer children
		if(*rule < *n.rule) {
			return true;
		}
		else {
			
			if(this->children.size() != n.children.size()) {
				return this->children.size() < n.children.size();
			}

			for(size_t i=0;i<n.children.size();i++) {
				if(this->child(i) < n.child(i)) 
					return true;
				else if (n.child(i) < this->child(i)) {
					return false;
				}
			}
			
			return false;
		}
	}
	
	nonterminal_t nt() const {
		/**
		 * @brief What nonterminal type do I return?
		 * @return 
		 */
		
		return this->rule->nt;
	}
	
	bool is_null() const { 
		/**
		 * @brief Am I a null node?
		 * @return 
		 */
	
		return this->rule == NullRule<VirtualMachineState_t>;
	}

	template<typename T>
	T sum(std::function<T(const Node<VirtualMachineState_t>&)>& f ) const {
		/**
		 * @brief Apply f to me and everything below me, adding up the result. 
		 * @param f
		 * @return 
		 */
		
		T s = f(*this);
		for(auto& c: this->children) {
			s += c.sum<T>(f);
		}
		return s;
	}

	template<typename T>
	T sum(T(*f)(const Node<VirtualMachineState_t>&) ) const {
		std::function ff = f;
		return sum(ff);
	}

	void map( const std::function<void(Node&)>& f) {
		/**
		 * @brief Apply f to me and everything below.  
		 * @param f
		 */
		
		f(*this);
		for(auto& c: this->children) {
			c.map(f); 
		}
	}
	
	virtual bool is_complete() const {
		/**
		 * @brief A tree is complete if it contains no null nodes below it. 
		 * @return 
		 */
		
		if(this->is_null()) return false;
		
		// does this have any subnodes below that are null?
		for(auto& c: this->children) {
			if(c.is_null()) return false; 
			if(not c.is_complete()) return false;
		}
		return this->children.size() == rule->N; // must have all my kids
	}
		
	
	virtual std::string string() const override { 
		/**
		 * @brief Convert a tree to a string, using each node's format.  
		 * @return 
		 */
		
		// Be careful to process the arguments in the right orders
		
		if(this->rule->N == 0) {
			assert(this->children.size() == 0 && "*** Should have zero children -- did you accidentally add children to a null node (that doesn't have a format)?");
			return this->rule->format;
		}
		else {
			
			std::vector<std::string> childStrings(this->children.size());
		
			for(size_t i=0;i<this->rule->N;i++) {
				childStrings[i] = this->children[i].string();
			}
			
			// now substitute the children into the format
			std::string s = this->rule->format;
			if(not this->can_resample) s = "\u2022"+s; // just to help out in some cases, we'll add this to nodes that we can't resample
			
			for(size_t i=0;i<this->rule->N;i++) {
				auto pos = s.find(Rule<VirtualMachineState_t>::ChildStr);
				assert(pos != std::string::npos && "Node format must contain one ChildStr (typically='%s') for each argument"); // must contain the ChildStr for all children all children
				s.replace(pos, Rule<VirtualMachineState_t>::ChildStr.length(), childStrings[i] );
			}
			return s;
		}
	}
	
	
	virtual std::string parseable() const {
		/**
		 * @brief Create a string that can be parsed according to Grammar.expand_from_names
		 * @return 
		 */
		
		// get a string like one we could parse
		std::string out = str(this->nt()) + NTDelimiter + this->rule->format;
		for(auto& c: this->children) {
			out += RuleDelimiter + c.parseable();
		}
		return out;
	}
	

	/********************************************************
	 * Operaitons for running programs
	 ********************************************************/

	virtual size_t program_size() const {
		/**
		 * @brief How big of a program does this correspond to? This is mostly the number of nodes, except that short-circuit evaluation of IF makes things a little more complex. 
		 * @return 
		 */
		
		// compute the size of the program -- just number of nodes plus special stuff for IF
		size_t n = 1; // I am one operation
		
		
		
		// TODO: FIX THIS PROGRAM-SIZE FOR IF ETC 
		
		
		//if( rule->instr.is_a(BuiltinOp::op_IF) ) { n += 1; } // I have to add this many more instructions for an if
		for(const auto& c: this->children) {
			n += c.program_size();
		}
		return n;
	}
	
	inline virtual int linearize(Program<VirtualMachineState_t> &ops) const { 
		/**
		 * @brief convert tree to a linear sequence of operations. 
		 * 		To do this, we first linearize the kids, leaving their values as the top on the stack
		 *		then we compute our value, remove our kids' values to clean up the stack, and push on our return
		 *		the only fanciness is for if: here we will use the following layout 
		 *		<TOP OF STACK> <bool> op_IF(xsize) X-branch JUMP(ysize) Y-branch
		 *		
		 *		NOTE: Inline here lets gcc inline a few recursions of this function, which ends up speeding
		 *		us up a bit (otherwise recursive inlining only happens at -O3)
		 *		This optimization is why we do set max-inline-insns-recursive in Fleet.mk
		 * @param ops
		 * @return This returns the *size* of the program that was pushed onto ops. This is useful for saving us
		 *         lots of calls to program_size, which ends up being an important optimization. 
		 */
				
		// NOTE: If you change the order here ever, you have to change how string() works so that 
		//       string order matches evaluation order
		// TODO: We should restructure this to use "map" so that the order is always the same as for printing
		
		
		// and just a little checking here
		for(size_t i=0;i<this->rule->N;i++) {
			assert(not this->children[i].is_null() && "Cannot linearize a Node with null children");
			assert(this->children[i].rule->nt == rule->type(i) && "Somehow the child has incorrect types -- this is bad news for you."); // make sure my kids types are what they should be
		}
		
		
		assert(rule != NullRule<VirtualMachineState_t> && "*** Cannot linearize if there is a null rule");
		
		// If we are an if, then we must do some fancy short-circuiting
//		if( rule->instr.is_a(BuiltinOp::op_IF) ) {
//			assert(rule->N == 3 && "BuiltinOp::op_IF require three arguments"); // must have 3 parts
//			
//			
//			int ysize = children[2].linearize(ops);
//			
//			// encode the jump
//			ops.emplace_back(BuiltinOp::op_JMP, ysize);
//			
//			int xsize = children[1].linearize(ops)+1; // must be +1 in order to skip over the JMP too
//			
//			// encode the if
//			ops.emplace_back(BuiltinOp::op_IF, xsize);
//			
//			// evaluate the bool first so its on the stack when we get to if
//			int boolsize = children[0].linearize(ops);
//			
//			return ysize + xsize + boolsize + 1; // +1 for if
//		}
//		else if( rule->instr.is_a(BuiltinOp::op_AND, BuiltinOp::op_OR)) {
//			// short circuit forms of and(x,y) and or(x,y)
//			assert(rule->N == 2 && "BuiltinOp::op_AND and BuiltinOp::op_OR require two arguments");
//			
//			// second arg pushed on first, on the bottom
//			int ysize = children[1].linearize(ops);
//			
//			if(rule->instr.is_a(BuiltinOp::op_AND)) {
//				ops.emplace_back(BuiltinOp::op_AND, ysize);
//			}
//			else {
//				assert(rule->instr.is_a(BuiltinOp::op_OR));
//				ops.emplace_back(BuiltinOp::op_OR, ysize);
//			}
//			
//			return children[0].linearize(ops)+ysize+1;			
//		}
//		else {
			/* Else we just process a normal child. 
			 * Here we push the children in increasing order. Then, when we pop rightmost first (as Primitive does), it 
			 * assigns the correct index.  */
			ops.emplace_back(this->rule->instr); 
			
			int mysize = 1; // one for my own instruction
			for(int i=this->rule->N-1;i>=0;i--) { // here we linearize right to left so that when we call right to left, it matches string order			
				mysize += this->children[i].linearize(ops);
			}
			return mysize; 
		//}
	}
		
	
	virtual bool operator==(const Node& n) const override {
		/**
		 * @brief Check equality between notes. Note this compares rule objects. 
		 * @param n
		 * @return 
		 */
		
		if(not (*rule == *n.rule))
			return false;
			
		if(this->children.size() != n.children.size())
			return false; 
	
		for(size_t i=0;i<this->hildren.size();i++){
			if(not (this->children[i] == n.children[i])) return false;
		}
		return true;
	}
	
	virtual size_t hash(size_t depth=0) const {
		/**
		 * @brief Hash a tree by hashing the rule and everything below. 
		 * @param depth
		 * @return 
		 */
		
		size_t output = rule->get_hash(); // tunrs out, this is actually important to prevent hash collisions when rule_id and i are small
		for(size_t i=0;i<this->children.size();i++) {
			hash_combine(output, depth, this->children[i].hash(depth+1), i); // modifies output
		}
		return output;
	}

	
	
};
