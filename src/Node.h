#pragma once

#include <functional>
#include <stack>

#include "Hash.h"

class Node {
	
public:
	Node* parent; 
	size_t pi; // what index am I in the parent?

	std::vector<Node> child;
	const Rule*  rule; // which rule did I use?
	double       lp; 
	bool         can_resample;
	
	
	class NodeIterator : public std::iterator<std::forward_iterator_tag, Node> {
		// Define an iterator class to make managing trees easier. 
		// This iterates in postfix order, which is standard in the library
		// because it is the order of linearization
		protected:
			Node*  current;
		
	public:
		
			NodeIterator(const Node* n) : current(n->left_descend()) { }
			Node& operator*() const  { return *current; }
			Node* operator->() const { return  current; }
			 
			NodeIterator& operator++(int blah) { this->operator++(); return *this; }
			NodeIterator& operator++() {
				if(current == nullptr) {
					return EndNodeIterator; 
				}
				if(current->is_root()) {
					current = nullptr;
					return EndNodeIterator;
				}
				
				if(current->pi+1 < current->parent->child.size()) {
					current = current->parent->child[current->pi+1].left_descend();
				}
				else { 
					// now we call the parent (if we're out of children)
					current = current->parent; 
				}
				return *this;
			}
				
			NodeIterator& operator+(size_t n) {
				for(size_t i=0;i<n;i++) {
					assert(not (*this == EndNodeIterator) && "Can't iterate past the end!");
					this->operator++();
					
				}
				return *this;
			}

			bool operator==(const NodeIterator& rhs) { return current == rhs.current; };
			bool operator!=(const NodeIterator& rhs) { return current != rhs.current; };
	};	

		
	static NodeIterator EndNodeIterator;
	
	Node(const Rule* r=nullptr, double _lp=0.0, bool cr=true) : 
		parent(nullptr), pi(0), child(r==nullptr ? 0 : r->N), rule(r==nullptr ? NullRule : r), lp(_lp), can_resample(cr) {	
		// NOTE: We don't allow parent to be set here bcause that maeks pi hard to set. We shuold only be placed
		// in trees with set_child
	}
	
	/* We must define our own copy and move since parent can't just be simply copied */
	
	Node(const Node& n) :
		parent(nullptr), child(n.child.size()), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		child = n.child;
		fix_child_info();
	}
	Node(Node&& n) :
		parent(nullptr), child(n.child.size()), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		child = std::move(n.child);
		fix_child_info();
	}
	
	void operator=(const Node& n) {
		parent = nullptr; 
		rule = n.rule;
		lp = n.lp;
		can_resample = n.can_resample;
		child = n.child;
		fix_child_info();
	}

	void operator=(Node&& n) {
		parent = nullptr; 
		rule = n.rule;
		lp = n.lp;
		can_resample = n.can_resample;

		child = std::move(n.child);
		fix_child_info();
	}
	
	NodeIterator begin() const { return Node::NodeIterator(this); }
	NodeIterator end()   const { return Node::EndNodeIterator; }
	
	Node* left_descend() const {
		Node* k = (Node*) this;
		while(k != nullptr && k->child.size() > 0) 
			k = &(k->child[0]);
		return k;
	}
	
	void fix_child_info() {
		// go through children and assign their parents to me
		// and fix their pi's
		for(size_t i=0;i<child.size();i++) {
			child[i].pi = i;
			child[i].parent = this;
		}
	}
	
//	size_t N() const { // how many children do I have? -- we don't define this because it' sambiguous between child.size() and rule->N, which may be different
//		return rule->N;
//	}
	nonterminal_t nt() const {
		return rule->nt;
	}
	
	Node& operator[](const size_t i) {
		return child.at(i); // at does bounds checking
	}
	
	const Node& operator[](const size_t i) const {
		return child.at(i); // at does bounds checking
	}
	
	void set_child(size_t i, Node& n) {
		// NOTE: if you add anything fancy to this, be sure to update the copy and move constructors
		
		while(child.size() <= i) // make it big enough for i  
			child.push_back(Node());

		child[i] = n;
		child[i].pi = i;
		child[i].parent = this;
	}
	void set_child(size_t i, Node&& n) {
		// NOTE: if you add anything fancy to this, be sure to update the copy and move constructors
		while(child.size() <= i) // make it big enough for i  
			child.push_back(Node());

		child[i] = n;
		child[i].pi = i;
		child[i].parent = this;
	}
	
	bool is_null() const { // am I the null rule?
		return rule == NullRule;
	}

	template<typename T>
	T sum(std::function<T(const Node&)>& f ) const {
		T s = f(*this);
		for(auto& c: child) {
			s += c.sum<T>(f);
		}
		return s;
	}

	void map( const std::function<void(Node&)>& f) {
		f(*this);
		for(auto& c: child) {
			c.map(f); 
		}
	}
	
	void map_const( const std::function<void(const Node&)>& f) const { // mapping that is constant
		f(*this);
		for(const auto& c: child) {
			c.map_const(f); 
		}
	}
	
	void rmap_const( const std::function<void(const Node&)>& f) const { // mapping that is constant
		for(int i=child.size()-1;i>=0;i--) {
			child[i].rmap_const(f); 
		}		
		f(*this);
	}
	
	virtual size_t count() const {
		std::function<size_t(const Node& n)> one = [](const Node& n){return (size_t)1;};
		return sum<size_t>( one );
	}
	
	virtual bool is_root() const {
		return parent == nullptr;
	}
	
	virtual bool is_terminal() const {
		return child.size() == 0;
	}
	
	virtual size_t count_terminals() const {
		size_t cnt = 0;
		for(const auto& n : *this) {
			if(n.is_terminal()) ++cnt;
		}
		return cnt;
	}
	
	virtual bool is_complete() const {
		
		// does this have any subnodes below that are null?
		for(auto& c: child) {
			if(c.is_null()) return false; 
			if(not c.is_complete()) return false;
		}
		return child.size() == rule->N; // must have all my kids
	}
		
	virtual Node* get_nth(int n, std::function<int(const Node&)>& f) {
		// return a pointer to the nth  child satisfying f (f's output cast to bool)
	
		for(auto& x : *this) {
			if(f(x)) {
				if(n == 0) return &x;
				else       --n;
			}
		}	
	
		return nullptr; // not here, losers
	}
	virtual Node* get_nth(int& n) { // default true on every node
		std::function<int(const Node&)> f = [](const Node& n) { return 1;};
		return get_nth(n, f); 
	}
	

	virtual std::string string() const { 
		// To convert to a string, we need to essentially simulate the evaluator
		// and be careful to process the arguments in the right orders
		
		if(rule->N == 0) {
			return rule->format;
		}
		else {
			
			std::string childStrings[child.size()];
		
			for(size_t i=0;i<rule->N;i++) {
				childStrings[i] = child[i].string();
			}
			
			// now substitute the children into the format
			std::string s = rule->format;
			if(not can_resample) s = "\u2022"+s; // just to help out in some cases, we'll add this to nodes that we can't resample
			
			for(size_t i=0;i<rule->N;i++) {
				auto pos = s.find(ChildStr);
				assert(pos != std::string::npos && "Node format must contain one ChildStr (typically='%s') for each argument"); // must contain the ChildStr for all children all children
				s.replace(pos, ChildStr.length(), childStrings[i] );
			}
			return s;
		}
	}
	
	
	virtual std::string parseable(std::string delim=":") const {
		// get a string like one we could parse
		std::string out = rule->format;
		for(auto& c: child) {
			out += delim + c.parseable(delim);
		}
		return out;
	}
	

	/********************************************************
	 * Operaitons for running programs
	 ********************************************************/

	virtual size_t program_size() const {
		// compute the size of the program -- just number of nodes plus special stuff for IF
		size_t n = 1; // I am one node
		if( rule->instr.is_a(BuiltinOp::op_IF) ) { n += 1; } // I have to add this many more instructions for an if
		for(auto& c: child) {
			n += c.program_size();
		}
		return n;
	}
		
	virtual void linearize(Program &ops) const { 
		// convert tree to a linear sequence of operations 
		// to do this, we first linearize the kids, leaving their values as the top on the stack
		// then we compute our value, remove our kids' values to clean up the stack, and push on our return
		// the only fanciness is for if: here we will use the following layout 
		// <TOP OF STACK> <bool> op_IF(xsize) X-branch JUMP(ysize) Y-branch
		
		// NOTE: If you change the order here ever, you have to change how string() works so that 
		//       string order matches evaluation order
		// TODO: We should restructure this to use "map" so that the order is always the same as for printing
		
		
		// and just a little checking here
		for(size_t i=0;i<rule->N;i++) {
			assert(not child[i].is_null() && "Cannot linearize a Node with null children");
			assert(child[i].rule->nt == rule->child_types[i] && "Somehow the child has incorrect types -- this is bad news for you."); // make sure my kids types are what they should be
		}
		
		
		// Main code
		if( rule->instr.is_a(BuiltinOp::op_IF) ) {
			assert(rule->N == 3 && "BuiltinOp::op_IF require three arguments"); // must have 3 parts
			
			int xsize = child[1].program_size()+1; // must be +1 in order to skip over the JMP too
			int ysize = child[2].program_size();
			assert(xsize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg. Your program is too large for Fleet."); // these sizes come from the arg bitfield 
			assert(ysize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg. Your program is too large for Fleet.");
			
			child[2].linearize(ops);
			
			// make the right instruction 
			// TODO: Assert that ysize fits 
			ops.push(Instruction(BuiltinOp::op_JMP,ysize));
//			ops.emplace_back(BuiltinOp::op_JMP, ysize);
			
			child[1].linearize(ops);
			
			// encode jump
			ops.push(Instruction(BuiltinOp::op_IF, xsize)); 
//			ops.emplace_back(BuiltinOp::op_IF, xsize);
			
			// evaluate the bool first so its on the stack when we get to if
			child[0].linearize(ops);
			
		}
//		else if(rule->instr.is_a(BuiltinOp::op_LAMBDA)) {
//			// Here we lay out in memory with LAMBDA<arg=function_length> function
//			// and then when we interpret, we allow ourselves to copy the function applied 
//			int fsize = child[0].program_size();
//			
//			child[0].linearize(ops);			
//			ops.push(Instruction(BuiltinOp::op_LAMBDA, fsize));
//			
//		}
		else {
			/* Here we push the children in increasing order. Then, when we pop rightmost first (as Primitive does), it 
			 * assigns the correct index.  */
			Instruction i = rule->instr; // use this as a template
			ops.push(i);			
//			ops.emplace_back(rule->instr); // these don't seem to speed thing up...
			
//			for(size_t i=0;i<rule->N;i++) {
			for(int i=rule->N-1;i>=0;i--) { // here we linearize right to left so that when we call right to left, it matches string order			
				child[i].linearize(ops);
			}
		}
		
	}
	
	virtual bool operator==(const Node& n) const{
		// Check equality between notes. Note that this compares the rule *pointers* so we need to be careful with 
		// serialization and storing/recovering full node trees with equality comparison
		
		if(not (*rule == *n.rule))
			return false;
			
		for(size_t i=0;i<rule->N;i++){
			if(not (child[i] == n.child[i])) return false;
		}
		return true;
	}

	virtual size_t hash(size_t depth=0) const {
		// hash and include
		size_t output = rule->get_hash(); // tunrs out, this is actually important to prevent hash collisions when rule_id and i are small
		for(size_t i=0;i<child.size();i++) {
			hash_combine(output, depth, child[i].hash(depth+1), i); // modifies output
		}
		return output;
	}

	
	
};


Node::NodeIterator Node::EndNodeIterator = NodeIterator(nullptr);