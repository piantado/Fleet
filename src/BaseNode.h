#pragma once 

#include<vector>
#include<string>

#include "Errors.h"
#include "IO.h"

/**
 * @class BaseNode
 * @author piantado
 * @date 03/07/20
 * @file BaseNode.h
 * @brief This is a general tree class, which we are adding because there are currently at least 3 different 
 * 		  tree classes used in different parts of fleet (Nodes, MCTS, data for BindingTheory, etc.). This is 
 * 		  an attempt to make a big superclass that puts all of this functionality in one place. It attempts 
 * 		  to keep the node size small so that we can use this efficiently in MCTS in particular. 
 */
template<typename this_t>
class BaseNode { 

protected:
	std::vector<this_t> children;
	
public:
	this_t* parent; 
	size_t pi; // what index am I in the parent?

	/**
	 * @brief Constructor of basenode -- sizes children to n 
	 * @param n
	 * @param p
	 * @param i
	 */	
	BaseNode(size_t n=0, this_t* p=nullptr, size_t i=0) : children(n), parent(p), pi(i) {
	}
	
	BaseNode(const this_t& n) {
		children = n.children;
		parent = n.parent;
		pi = n.pi;  
		fix_child_info();
	}
	BaseNode(this_t&& n) {
		parent = n.parent;
		pi = n.pi;  
		children = std::move(n.children);
		fix_child_info();
	}
	
	void operator=(const this_t& t) {
		parent = t.parent;
		pi = t.pi;
		children = t.children;
		fix_child_info();
	}
	void operator=(const this_t&& t) {
		parent = t.parent;
		pi = t.pi;
		children = std::move(t.children);
		fix_child_info();
	}
	

	/**
	 * @brief Make a node root -- just nulls the parent
	 */
	void make_root() {
		parent = nullptr;
		pi = 0;
	}

	virtual ~BaseNode() {}

	// Functions to be defined by subclasses
	virtual std::string string(bool usedot=true) const {
		throw YouShouldNotBeHereError("*** BaseNode subclass has no defined string()"); 
	}
	virtual std::string my_string() const { /// just print myself, not all my kids (single row per node display)
		throw YouShouldNotBeHereError("*** BaseNode subclass has no defined my_string()"); 
	}
	virtual bool operator==(const this_t& n) const {
		throw YouShouldNotBeHereError("*** BaseNode subclass has no defined operator==");
	}

	////////////////////////////////////////////////////////////////////////////
	/**
	 * @class NodeIterator
	 * @author piantado
	 * @date 03/07/20
	 * @file BaseNode.h
	 * @brief Define an interator for nodes. This iterates in prefix order, which is the standard in the library
	 * 		  because it's used for linearization
	 */	
	class NodeIterator : public std::iterator<std::forward_iterator_tag, this_t> {
		// Define an iterator class to make managing trees easier. 
		// This iterates in postfix order, which is standard in the library
		// because it is the order of linearization
		protected:
			this_t*  current;
			
			// we need to store the start because when we start at a subtree, we want to iteratre through its subnodes
			// and so we need to know when to stop
			const this_t* start; 
		
	public:
		
			NodeIterator(const this_t* n) : current( n!= nullptr ? n->left_descend() : nullptr), start(n) { }
			this_t& operator*() const  { 
				assert(current != nullptr);
				return *current; 
			}
			this_t* operator->() const { 
				assert(current != nullptr);
				return  current; 
			}
			 
			NodeIterator& operator++(int blah) { this->operator++(); return *this; }
			NodeIterator& operator++() {
				assert(current != nullptr);
				assert(not (*this == EndNodeIterator) && "Can't iterate past the end!");
				if(current == nullptr or current == start or current->is_root()) {
					current = nullptr; 
					return EndNodeIterator; 
				}
				
				// go through the children if we can
				if(current->pi+1 < current->parent->children.size()) {
					current = current->parent->children[current->pi+1].left_descend();
				}
				else { 	// now we call the parent (if we're out of children)
					current = current->parent; 
				}
				return *this;
			}
				
			NodeIterator& operator+(size_t n) {
				for(size_t i=0;i<n;i++) this->operator++();					
				return *this;
			}

			bool operator==(const NodeIterator& rhs) { return current == rhs.current; }
			//bool operator!=(const NodeIterator& rhs) { return current != rhs.current; }
	};	
	static NodeIterator EndNodeIterator; // defined below
	////////////////////////////////////////////////////////////////////////////
	
	NodeIterator begin() const { return BaseNode<this_t>::NodeIterator(static_cast<const this_t*>(this)); }
	NodeIterator end()   const { return BaseNode<this_t>::EndNodeIterator; }

	virtual bool operator!=(const this_t& n) const{
		return not (*this == n);
	}

	void reserve_children(const size_t n) {
		children.reserve(n);
	}

	decltype(children)& get_children() {
		return children;
	}
	const decltype(children)& get_children() const {
		return children;
	}

	this_t& child(const size_t i) {
		/**
		 * @brief Get a reference to my i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i);
	}
	
	const this_t& child(const size_t i) const {
		/**
		 * @brief Constant reference to the i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i);
	}
	
	template<typename... Args>
	void fill(size_t n, Args... args) {
		/**
		 * @brief Fill in all of my immediate children with Null nodes (via NullRule)
		 */
		
		// ensure that all of my children are empty nodes
		for(size_t i=0;i<n;i++) {
			set_child(i, this_t(args...));
		}
	}
	
	
	size_t nchildren() const {
		/**
		 * @brief How many children do I have?
		 * @return 
		 */
		
		return children.size();
	}
	
	this_t* left_descend() const {
		/**
		 * @brief Go down a tree and find the leftmost child
		 * @return 
		 */
		
		this_t* k = const_cast<this_t*>(static_cast<const this_t*>(this));
		while(k != nullptr and k->nchildren() > 0) {
			k = &(k->child(0));
		}
		return k;
	}
	
	size_t depth() const {
		size_t d = 0;
		for(const auto& c: children) {
			d = std::max(d, c.depth()+1);
		}
		return d;
	}
	
	void fix_child_info() {
		/**
		 * @brief Fix my immediate children's pointers to ensure that children's parent pointers and indices are correct. 
		 */
		
		// go through children and assign their parents to me
		// and fix their pi's
		size_t i = 0;
		for(auto& c : children) {
			c.pi = i;
			c.parent = static_cast<this_t*>(this);
			i++;
		}
	}

	void check_child_info() const {
		/**
		 * @brief assert that all of the child info is correct
		 */
		
		size_t i = 0;
		for(const auto& c : children) {
			
			// check that the kids point to the right things
			assert(c.pi == i);
			assert(c.parent == this);			
			i++;
		}
	}


	this_t& operator[](const size_t i) {
		/**
		 * @brief Index my i'th child
		 * @param i
		 * @return 
		 */
		
		return children.at(i); // at does bounds checking
	}
	
	const this_t& operator[](const size_t i) const {
		return children.at(i); 
	}
	
	void set_child(const size_t i, this_t& n) {
		/**
		 * @brief Set my child to n. NOTE: This one needs to be used, rather than accessing children directly, because we have to set parent pointers and indices. 
		 * @param i
		 * @param n
		 */
		
		while(children.size() <= i) // make it big enough for i  
			children.push_back(this_t());

		children[i] = n;
		children[i].pi = i;
		children[i].parent = static_cast<this_t*>(this);
	}
	void set_child(const size_t i, this_t&& n) {
		/**
		 * @brief Child setter for move
		 * @param i
		 */
		
		// NOTE: if you add anything fancy to this, be sure to update the copy and move constructors
		
		while(children.size() <= i) // make it big enough for i  
			children.push_back(this_t());

		children[i] = n;
		children[i].pi = i;
		children[i].parent = static_cast<this_t*>(this);
	}
	
	void push_back(this_t& n) {
		set_child(children.size(), n);
	}
	void push_back(this_t&& n) {
		set_child(children.size(), n);
	}

	/**
	 * @brief Am I a root node? I am if my parent is nullptr. 
	 * @return 
	 */	
	virtual bool is_root() const {		
		return parent == nullptr;
	}
	
	
	/**
	 * @brief Find the root of this node by walking up the tree
	 * @return 
	 */	
	this_t* root() {
		this_t* x = static_cast<this_t*>(this);
		while(x->parent != nullptr) {
			x = x->parent;
		}
		return x;
	}
	
	/**
	 * @brief Return a pointer to the first node satisfying predicate f, in standard traversal; nullptr otherwise
	 * @param f
	 * @return 
	 */	
	this_t* get_via(std::function<bool(this_t&)>& f ) {
		for(auto& n : *this) {
			if(f(n)) return &n;
		}
		return nullptr; 
	}
	
	/**
	 * @brief How many nodes total are below me?
	 * @param n
	 * @return 
	 */	
	virtual size_t count() const {

		size_t n=1; // me
		for(auto& c : children) {
			n += c.count();			
		}
		return n;
	}
	
	/**
	 * @brief How many nodes below me are equal to n?
	 * @param n
	 * @return 
	 */
	virtual size_t count(const this_t& n) const {
		size_t cnt = (n == *static_cast<const this_t*>(this));
		for(auto& c : children) {
			cnt += c.count(n);
		}
		return cnt;
	}

	/**
	 * @brief Am I a terminal? I am if I have no children. 
	 * @return 
	 */
	virtual bool is_terminal() const {
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
	
	virtual this_t* get_nth(int n, std::function<int(const this_t&)>& f) {
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
	virtual this_t* get_nth(int n) { // default true on every node
		std::function<int(const this_t&)> f = [](const this_t& x) { return 1;};
		return get_nth(n, f); 
	}
	
	
	template<typename T>
	T sum(std::function<T(const this_t&)>& f ) const {
		/**
		 * @brief Apply f to me and everything below me, adding up the result. 
		 * @param f
		 * @return 
		 */
		
		T s = f(* dynamic_cast<const this_t*>(this)); // a little ugly here because it needs to be the subclass type
		for(auto& c: this->children) {
			s += c.sum(f);
		}
		return s;
	}

	template<typename T>
	T sum(T(*f)(const this_t&) ) const {
		std::function ff = f;
		return sum(ff);
	}
	
	
	bool all(std::function<bool(const this_t&)>& f ) const {
		/**
		 * @brief Check if f is true of me and every node below 
		 * @param f
		 * @return 
		 */
		
		if(not f(* dynamic_cast<const this_t*>(this))) 
			return false;
			
		for(auto& c: this->children) {
			if(not c.all(f)) 
				return false;
		}
		return true;
	}

	void map( const std::function<void(this_t&)>& f) {
		/**
		 * @brief Apply f to me and everything below.  
		 * @param f
		 */
		
		f(* dynamic_cast<const this_t*>(this));
		for(auto& c: this->children) {
			c.map(f); 
		}
	}
	
	
	void print(size_t t=0) const {
		
		std::string tabs(t,'\t');		
		
		COUT tabs << this->my_string() ENDL;
		for(auto& c : children) {
			c.print(t+1);
		}		
	}
	
};


template<typename this_t>
typename BaseNode<this_t>::NodeIterator BaseNode<this_t>::EndNodeIterator = NodeIterator(nullptr);



template<typename this_t>
std::ostream& operator<<(std::ostream& o, const BaseNode<this_t>& t) {
	o << t.string();
	return o;
}