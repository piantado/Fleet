#pragma once 

#include<vector>
#include<string>

#include "Errors.h"

// This is a general tree class, which we are adding because there are currently at least 3 different tree classes used
// in different parts of fleet (Nodes, MCTS, data for BindingTheory, etc.). This is an attempt to make a big superclass
// that puts all of this functionality in one place. It attempts to keep the node size small so that we can use
// this efficiently in MCTS in particular 

template<typename this_t>
class Tree { 

protected:
	std::vector<this_t> children; // TODO make this protected so we don't set children -- must use set_child
	
public:
	this_t* parent; 
	size_t pi; // what index am I in the parent?

	// TODO: ADD CONSTRUCTORS

	Tree(size_t n=0, this_t* p=nullptr, size_t i=0) : children(n), parent(p), pi(i) {
		
	}
	Tree(const this_t& n) {
		children = n.children;
		parent = n.parent;
		pi = n.pi;  
		fix_child_info();
	}
	Tree(this_t&& n) {
		parent = n.parent;
		pi = n.pi;  
		children = std::move(n.children);
		fix_child_info();
	}
	
	
	virtual ~Tree() {}

	// Functions to be defined by subclasses
	virtual std::string string() const {
		throw YouShouldNotBeHereError("*** Tree subclass has no defined string()"); 
	}
	virtual bool operator==(const this_t& n) const {
		throw YouShouldNotBeHereError("*** Tree subclass has no defined operator==");
	}

	////////////////////////////////////////////////////////////////////////////
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
		
			NodeIterator(const this_t* n) : current(n->left_descend()), start(n) { }
			this_t& operator*() const  { 
				return *current; 
			}
			this_t* operator->() const { 
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
	static NodeIterator EndNodeIterator; // defined below
	
	NodeIterator begin() const { return Tree<this_t>::NodeIterator(static_cast<const this_t*>(this)); }
	NodeIterator end()   const { return Tree<this_t>::EndNodeIterator; }


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
		
		this_t* k = (this_t*) this;
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
		for(auto& c : children) {
			
			// check that the kids point to the right things
			assert(c.pi == i);
			assert(c.parent == this);
			
			// and that they are of the right type
			assert(c.rule->nt == this->rule->type(i));

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
	
	void set_child(size_t i, this_t& n) {
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
	void set_child(size_t i, this_t&& n) {
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
	
	virtual bool is_root() const {
		/**
		 * @brief Am I a root node? I am if my parent is nullptr. 
		 * @return 
		 */
		
		return parent == nullptr;
	}
	
	virtual size_t count() const {
		/**
		 * @brief How many nodes total are below me?
		 * @param n
		 * @return 
		 */
		size_t n=1; // me
		for(auto& c : children) {
			n += c.count();			
		}
		return n;
	}
	
	virtual size_t count(const this_t& n) const {
		/**
		 * @brief How many nodes below me are equal to n?
		 * @param n
		 * @return 
		 */
		
		size_t cnt = (n == *static_cast<const this_t*>(this));
		for(auto& c : children) {
			cnt += c.count(n);
		}
		return cnt;
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
		std::function<int(const this_t&)> f = [](const this_t& n) { return 1;};
		return get_nth(n, f); 
	}
};


template<typename this_t>
Tree<this_t>::NodeIterator Tree<this_t>::EndNodeIterator = NodeIterator(nullptr);



template<typename this_t>
std::ostream& operator<<(std::ostream& o, const Tree<this_t>& t) {
	o << t.string();
	return o;
}