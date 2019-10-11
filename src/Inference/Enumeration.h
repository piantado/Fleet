#pragma once

/* Functions for enumerating trees */

// Add: 
// grammar.next_rule -- gives the next rule in the ordering
// grammar.base_tree -- give the 0th tree, meaning the one that expands to terminals as quickly as possible
// Change:
// grammar should sort the rules so the terminals are first, and higher probability is first 



Node increment_tree(Node* t, const Grammar& g) {
	// take a tree t and return a new tree that is t "plus one" 
	// NOTE: Here we modify t
	
	if(t->is_root()) {
		
	}
	
	Node* v = out.left_descend(); // find the leftmost child
	
	Rule* r = g.next_rule(v->parent->rule);
	
}


