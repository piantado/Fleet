#pragma once

#include <functional>
#include <stack>

#include "Hash.h"

class Node {
	
	
public:
	Node* parent; 
	size_t pi; // what index am I in the parent?

	// These are used in parseable to delimit nodes nonterminals and multiple nodes in a tree
	const static char NTDelimiter = ':'; // delimit nt:format 
	const static char RuleDelimiter = ';'; // delimit a sequence of nt:format;nt:format; etc


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
			
			// we need to store the start because when we start at a subtree, we want to iteratre through its subnodes
			// and so we need to know when to stop
			const Node* start; 
		
	public:
		
			NodeIterator(const Node* n) : current(n->left_descend()), start(n) { }
			Node& operator*() const  { 
				return *current; 
			}
			Node* operator->() const { 
				return  current; 
			}
			 
			NodeIterator& operator++(int blah) { this->operator++(); return *this; }
			NodeIterator& operator++() {
				assert(not (*this == EndNodeIterator) && "Can't iterate past the end!");
				if(current == nullptr or current == start or current->is_root()) {
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

			bool operator==(const NodeIterator& rhs) { return current == rhs.current; }
			bool operator!=(const NodeIterator& rhs) { return current != rhs.current; }
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
		/**
		 * @brief Get a reference to my i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i);
	}
	
	const Node& child(const size_t i) const {
		/**
		 * @brief Constant reference to the i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i);
	}
	
	nonterminal_t type(const size_t i) const {
		/**
		 * @brief Return the type of the i'th child
		 * @param i
		 * @return 
		 */
		
		return rule->type(i);
	}
	
	size_t nchildren() const {
		/**
		 * @brief How many children do I have?
		 * @return 
		 */
		
		return children.size();
	}
	
	void fill() {
		/**
		 * @brief Fill in all of my immediate children with Null nodes (via NullRule)
		 */
		
		// ensure that all of my children are empty nodes
		for(size_t i=0;i<rule->N;i++) {
			set_child(i, Node());
		}
	}
	
	void operator=(const Node& n) {
		parent = n.parent; 
		pi = n.pi;
		rule = n.rule;
		lp = n.lp;
		can_resample = n.can_resample;
		children = n.children;
		fix_child_info();
	}

	void operator=(Node&& n) {
		parent = n.parent; 
		pi = n.pi;
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
		/**
		 * @brief Go down a tree and find the leftmost child
		 * @return 
		 */
		
		Node* k = (Node*) this;
		while(k != nullptr && k->children.size() > 0) 
			k = &(k->children[0]);
		return k;
	}
	
	void fix_child_info() {
		/**
		 * @brief Fix my immediate children's pointers to ensure that children's parent pointers and indices are correct. 
		 */
		
		// go through children and assign their parents to me
		// and fix their pi's
		for(size_t i=0;i<children.size();i++) {
			children[i].pi = i;
			children[i].parent = this;
		}
	}
	
	nonterminal_t nt() const {
		/**
		 * @brief What nonterminal type do I return?
		 * @return 
		 */
		
		return rule->nt;
	}
	
	Node& operator[](const size_t i) {
		/**
		 * @brief Index my i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i); // at does bounds checking
	}
	
	const Node& operator[](const size_t i) const {
		return children.at(i); 
	}
	
	void set_child(size_t i, Node& n) {
		/**
		 * @brief Set my child to n. NOTE: This one needs to be used, rather than accessing children directly, because we have to set parent pointers and indices. 
		 * @param i
		 * @param n
		 */
		
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
		/**
		 * @brief Child setter for move
		 * @param i
		 */
		
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
		/**
		 * @brief Set myself to n. This should be used so that parent pointers etc. can be updated. 
		 * @param n
		 */
		
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
	
	void set_to(Node&& n) {
		/**
		 * @brief Set myself to n (move version)
		 */
		
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
	
	bool is_null() const { 
		/**
		 * @brief Am I a null node?
		 * @return 
		 */
	
		return rule == NullRule;
	}

	template<typename T>
	T sum(std::function<T(const Node&)>& f ) const {
		/**
		 * @brief Apply f to me and everything below me, adding up the result. 
		 * @param f
		 * @return 
		 */
		
		T s = f(*this);
		for(auto& c: children) {
			s += c.sum<T>(f);
		}
		return s;
	}

	template<typename T>
	T sum(T(*f)(const Node&) ) const {
		/**
		 * @brief Apply f to me and everything below me, adding up the result. 
		 * @param f
		 * @return 
		 */
		T s = f(*this);
		for(auto& c: children) {
			s += c.sum<T>(f);
		}
		return s;
	}


	void map( const std::function<void(Node&)>& f) {
		/**
		 * @brief Apply f to me and everything below.  
		 * @param f
		 */
		
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
		/**
		 * @brief How many nodes are below me?
		 * @return 
		 */
		
		std::function<size_t(const Node& n)> one = [](const Node& n){return (size_t)1;};
		return sum<size_t>( one );
	}
	
	virtual size_t count(const Node& n) const {
		/**
		 * @brief How many nodes below me are equal to n?
		 * @param n
		 * @return 
		 */
		
		size_t cnt = 0;
		for(auto& x : *this) {
			if(x == n) cnt++;
		}
		return cnt;
	}
	
	virtual bool is_root() const {
		/**
		 * @brief Am I a root node? I am if my parent is nullptr. 
		 * @return 
		 */
		
		return parent == nullptr;
	}
	
	virtual bool is_terminal() const {
		/**
		 * @brief Am I a terminal? I am if I have no children. 
		 * @return 
		 */
		
		return children.size() == 0;
	}
	
	virtual size_t count_terminals() const {
		/**
		 * @brief How many terminals are below me?
		 * @return 
		 */
		
		size_t cnt = 0;
		for(const auto& n : *this) {
			if(n.is_terminal()) ++cnt;
		}
		return cnt;
	}
	
	virtual bool is_complete() const {
		/**
		 * @brief A tree is complete if it contains no null nodes below it. 
		 * @return 
		 */
		
		if(is_null()) return false;
		
		// does this have any subnodes below that are null?
		for(auto& c: children) {
			if(c.is_null()) return false; 
			if(not c.is_complete()) return false;
		}
		return children.size() == rule->N; // must have all my kids
	}
		
	virtual Node* get_nth(int n, std::function<int(const Node&)>& f) {
		/**
		 * @brief Return a pointer to the n'th child satisfying f (f's output is cast to bool)
		 * @param n
		 * @param f
		 * @return 
		 */
		
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
		/**
		 * @brief Convert a tree to a string, using each node's format.  
		 * @return 
		 */
		
		// Be careful to process the arguments in the right orders
		
		if(rule->N == 0) {
			assert(children.size() == 0 && "*** Should have zero children -- did you accidentally add children to a null node (that doesn't have a format)?");
			return rule->format;
		}
		else {
			
			std::vector<std::string> childStrings(children.size());
		
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
		/**
		 * @brief Create a string that can be parsed according to Grammar.expand_from_names
		 * @return 
		 */
		
		// get a string like one we could parse
		std::string out = str(this->nt()) + NTDelimiter + rule->format;
		for(auto& c: children) {
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
		size_t n = 1; // I am one node
		if( rule->instr.is_a(BuiltinOp::op_IF) ) { n += 1; } // I have to add this many more instructions for an if
		for(auto& c: children) {
			n += c.program_size();
		}
		return n;
	}
		
	inline virtual void linearize(Program &ops) const { 
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
 */
		
		//  

		
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
		/**
		 * @brief Check equality between notes. Note this compares rule objects. 
		 * @param n
		 * @return 
		 */
		
		if(not (*rule == *n.rule))
			return false;
			
		if(children.size() != n.children.size())
			return false; 
	
		for(size_t i=0;i<children.size();i++){
			if(not (children[i] == n.children[i])) return false;
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
		for(size_t i=0;i<children.size();i++) {
			hash_combine(output, depth, children[i].hash(depth+1), i); // modifies output
		}
		return output;
	}

	
	
};


Node::NodeIterator Node::EndNodeIterator = NodeIterator(nullptr);


std::ostream& operator<<(std::ostream& o, const Node& n) {
	o << n.string();
	return o;
}