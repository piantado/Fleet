#pragma once
#include "Node.h"

class Node::NodeIterator : public std::iterator<std::forward_iterator_tag, Node> {
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
			for(size_t i=0;i<n;i++)
				this->operator++();
			return *this;
		}

		bool operator==(const NodeIterator& rhs) { return current == rhs.current; };
		bool operator!=(const NodeIterator& rhs) { return current != rhs.current; };
};	


Node::NodeIterator Node::EndNodeIterator = NodeIterator(nullptr);