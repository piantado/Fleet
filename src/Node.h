#pragma once

#include <functional>
#include <stack>

#include "Hash.h"

class Node {
	
public:
	Node* parent; 
	size_t pi; // what index am I in the parent?

protected:
	std::vector<Node> children; // TODO make this protected so we don't set children -- must use set_child
	
public:
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
				assert(not (*this == EndNodeIterator) && "Can't iterate past the end!");
				if(current == nullptr) {
					return EndNodeIterator; 
				}
				if(current->is_root()) {
					current = nullptr;
					return EndNodeIterator;
				}
				
				if(current->pi+1 < current->parent->children.size()) {
					current = current->parent->children[current->pi+1].left_descend();
				}
				else { 
					// now we call the parent (if we're out of children)
					current = current->parent; 
				}
				return *this;
			}
				
			NodeIterator& operator+(size_t n) {
				for(size_t i=0;i<n;i++) {
					this->operator++();
					
				}
				return *this;
			}

			bool operator==(const NodeIterator& rhs) { return current == rhs.current; };
			bool operator!=(const NodeIterator& rhs) { return current != rhs.current; };
	};	
	static NodeIterator EndNodeIterator;
	
	
	Node(const Rule* r=nullptr, double _lp=0.0, bool cr=true) : 
		parent(nullptr), pi(0), children(r==nullptr ? 0 : r->N), rule(r==nullptr ? NullRule : r), lp(_lp), can_resample(cr) {	
		// NOTE: We don't allow parent to be set here bcause that maeks pi hard to set. We shuold only be placed
		// in trees with set_child
	}
	
	/* We must define our own copy and move since parent can't just be simply copied */	
	Node(const Node& n) :
		parent(nullptr), children(n.children.size()), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		children = n.children;
		fix_child_info();
	}
	Node(Node&& n) :
		parent(nullptr), children(n.children.size()), rule(n.rule), lp(n.lp), can_resample(n.can_resample) {
		children = std::move(n.children);
		fix_child_info();
	}
	
	Node& child(const size_t i) {
		return children.at(i);
	}
	
	const Node& child(const size_t i) const {
		return children.at(i);
	}
	
	nonterminal_t type(const size_t i) const {
		return rule->type(i);
	}
	
	const size_t nchildren() const {
		return children.size();
	}
	
	void fill() {
		// ensure that all of my children are empty nodes
		for(size_t i=0;i<rule->N;i++) {
			set_child(i, Node());
		}
	}
	
	void operator=(const Node& n) {
		parent = nullptr; 
		rule = n.rule;
		lp = n.lp;
		can_resample = n.can_resample;
		children = n.children;
		fix_child_info();
	}

	void operator=(Node&& n) {
		parent = nullptr; 
		rule = n.rule;
		lp = n.lp;
		can_resample = n.can_resample;

		children = std::move(n.children);
		fix_child_info();
	}
	
	bool operator<(const Node& n) const {
		// for sorting/storing in sets -- for now just inherit the rule sort
		return *rule < *n.rule;
	}
	
	NodeIterator begin() const { return Node::NodeIterator(this); }
	NodeIterator end()   const { return Node::EndNodeIterator; }
	
	Node* left_descend() const {
		Node* k = (Node*) this;
		while(k != nullptr && k->children.size() > 0) 
			k = &(k->children[0]);
		return k;
	}
	
	void fix_child_info() {
		// go through children and assign their parents to me
		// and fix their pi's
		for(size_t i=0;i<children.size();i++) {
			children[i].pi = i;
			children[i].parent = this;
		}
	}
	
	nonterminal_t nt() const {
		return rule->nt;
	}
	
	Node& operator[](const size_t i) {
		return children.at(i); // at does bounds checking
	}
	
	const Node& operator[](const size_t i) const {
		return children.at(i); 
	}
	
	void set_child(size_t i, Node& n) {
		// NOTE: if you add anything fancy to this, be sure to update the copy and move constructors

		assert(i < rule->N);
		assert(n.nt() == rule->type(i));
		
		while(children.size() <= i) // make it big enough for i  
			children.push_back(Node());

		children[i] = n;
		children[i].pi = i;
		children[i].parent = this;
	}
	void set_child(size_t i, Node&& n) {
		// NOTE: if you add anything fancy to this, be sure to update the copy and move constructors
		
		assert(i < rule->N);
		assert(n.nt() == rule->type(i));
		
		while(children.size() <= i) // make it big enough for i  
			children.push_back(Node());

		children[i] = n;
		children[i].pi = i;
		children[i].parent = this;
	}
	
	void set_to(Node& n) {
		// sometimes it is easier to set a node rather than setting on parent
		// this conveniently handles the case when parent is nullptr
		
		
		if(parent == nullptr) {
			assert(n.nt() == nt());
			*this = n;
			parent = nullptr;
		}
		else {
			assert(parent->type(pi) == n.nt());
			parent->set_child(pi, n);
		}
	}
	
	bool is_null() const { // am I the null rule?
		return rule == NullRule;
	}

	template<typename T>
	T sum(std::function<T(const Node&)>& f ) const {
		T s = f(*this);
		for(auto& c: children) {
			s += c.sum<T>(f);
		}
		return s;
	}

	template<typename T>
	T sum(T(*f)(const Node&) ) const {
		T s = f(*this);
		for(auto& c: children) {
			s += c.sum<T>(f);
		}
		return s;
	}


	void map( const std::function<void(Node&)>& f) {
		f(*this);
		for(auto& c: children) {
			c.map(f); 
		}
	}
	
	void map_const( const std::function<void(const Node&)>& f) const { // mapping that is constant
		f(*this);
		for(const auto& c: children) {
			c.map_const(f); 
		}
	}
	
	void rmap_const( const std::function<void(const Node&)>& f) const { // mapping that is constant
		for(int i=children.size()-1;i>=0;i--) {
			children[i].rmap_const(f); 
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
		return children.size() == 0;
	}
	
	virtual size_t count_terminals() const {
		size_t cnt = 0;
		for(const auto& n : *this) {
			if(n.is_terminal()) ++cnt;
		}
		return cnt;
	}
	
	virtual bool is_complete() const {
		
		if(is_null()) return false;
		
		// does this have any subnodes below that are null?
		for(auto& c: children) {
			if(c.is_null()) return false; 
			if(not c.is_complete()) return false;
		}
		return children.size() == rule->N; // must have all my kids
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
	virtual Node* get_nth(int n) { // default true on every node
		std::function<int(const Node&)> f = [](const Node& n) { return 1;};
		return get_nth(n, f); 
	}

	virtual std::string string() const { 
		// To convert to a string, we need to essentially simulate the evaluator
		// and be careful to process the arguments in the right orders
		
		if(rule->N == 0) {
			assert(children.size() == 0 && "*** Should have zero children -- did you accidentally add children to a null node (that doesn't have a format)?");
			return rule->format;
		}
		else {
			
			std::string childStrings[children.size()];
		
			for(size_t i=0;i<rule->N;i++) {
				childStrings[i] = children[i].string();
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
	
	
	virtual std::string parseable() const {
		// get a string like one we could parse
		std::string out = str(this->nt()) + ":" + rule->format;
		for(auto& c: children) {
			out += ";" + c.parseable();
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
		for(auto& c: children) {
			n += c.program_size();
		}
		return n;
	}
		
	inline virtual void linearize(Program &ops) const { 
		// convert tree to a linear sequence of operations 
		// to do this, we first linearize the kids, leaving their values as the top on the stack
		// then we compute our value, remove our kids' values to clean up the stack, and push on our return
		// the only fanciness is for if: here we will use the following layout 
		// <TOP OF STACK> <bool> op_IF(xsize) X-branch JUMP(ysize) Y-branch
		
		// NOTE: Inline here lets gcc inline a few recursions of this function, which ends up speeding
		// us up a bit (otherwise recursive inlining only happens at -O3)
		// This optimization is why we do set max-inline-insns-recursive in Fleet.mk
		
		// NOTE: If you change the order here ever, you have to change how string() works so that 
		//       string order matches evaluation order
		// TODO: We should restructure this to use "map" so that the order is always the same as for printing
		
		
		// and just a little checking here
		for(size_t i=0;i<rule->N;i++) {
			assert(not children[i].is_null() && "Cannot linearize a Node with null children");
			assert(children[i].rule->nt == rule->type(i) && "Somehow the child has incorrect types -- this is bad news for you."); // make sure my kids types are what they should be
		}
		
		
		// Main code
		if( rule->instr.is_a(BuiltinOp::op_IF) ) {
			assert(rule->N == 3 && "BuiltinOp::op_IF require three arguments"); // must have 3 parts
			
			int xsize = children[1].program_size()+1; // must be +1 in order to skip over the JMP too
			assert(xsize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg. Your program is too large for Fleet."); // these sizes come from the arg bitfield 
			
			int ysize = children[2].program_size();
			assert(ysize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg. Your program is too large for Fleet.");
			
			children[2].linearize(ops);
			
			// make the right instruction 
			// TODO: Assert that ysize fits 
//			ops.push(Instruction(BuiltinOp::op_JMP,ysize));
			ops.emplace_back(BuiltinOp::op_JMP, ysize);
			
			children[1].linearize(ops);
			
			// encode jump
//			ops.push(Instruction(BuiltinOp::op_IF, xsize)); 
			ops.emplace_back(BuiltinOp::op_IF, xsize);
			
			// evaluate the bool first so its on the stack when we get to if
			children[0].linearize(ops);
			
		}
		else {
			/* Here we push the children in increasing order. Then, when we pop rightmost first (as Primitive does), it 
			 * assigns the correct index.  */
			ops.emplace_back(rule->instr); 
			
			for(int i=rule->N-1;i>=0;i--) { // here we linearize right to left so that when we call right to left, it matches string order			
				children[i].linearize(ops);
			}
		}
		
	}
	
	virtual bool operator==(const Node& n) const{
		// Check equality between notes. Note that this compares the rule *pointers* so we need to be careful with 
		// serialization and storing/recovering full node trees with equality comparison
		
		if(not (*rule == *n.rule))
			return false;
			
		if(children.size() != n.children.size())
			return false; 
	
		for(size_t i=0;i<rule->N;i++){
			if(not (children[i] == n.children[i])) return false;
		}
		return true;
	}

	virtual size_t hash(size_t depth=0) const {
		// hash and include
		size_t output = rule->get_hash(); // tunrs out, this is actually important to prevent hash collisions when rule_id and i are small
		for(size_t i=0;i<children.size();i++) {
			hash_combine(output, depth, children[i].hash(depth+1), i); // modifies output
		}
		return output;
	}

	
	
};


Node::NodeIterator Node::EndNodeIterator = NodeIterator(nullptr);


std::ostream& operator<<(std::ostream& o, Node& n) {
	o << n.string();
	return o;
}