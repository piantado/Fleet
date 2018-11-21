#ifndef PROPOSERS_H
#define PROPOSERS_H

#include<utility>
#include<tuple>
#include "Miscellaneous.h"

// TOOD: We could do insert/delete with entire trees -- replace any tree down below?


std::pair<Node*,double> regeneration_proposal(Grammar* grammar, Node* from) {
	// copy, regenerate a random node, and return that and forward-backward prob
	
	std::function<int(const Node* n)> resample_counter = [](const Node* n) { return n->can_resample*1;};
	int N = from->sum<int>(resample_counter);
			
	auto ret = from->copy();

	if(N == 0) {
		return std::make_pair(ret, 0.0);
	}
	
	int w = myrandom<int>( N); // which do I replace?

	Node* n = ret->get_nth(w, resample_counter); // get the nth in ret		
	Node* g = grammar->generate<Node>(n->rule->nonterminal_type); // make something new of the same type
	n->takeover(g); // and take its resources, zeroing it before deleting
	delete g;
	
	double fb =   (-log( from->sum<int>(resample_counter) ) + grammar->log_probability(ret)) 
			     - 
		          (-log(  ret->sum<int>(resample_counter) ) + grammar->log_probability(from));
				
	return std::make_pair(ret, fb);
}


std::pair<Node*, double> insert_proposal(Grammar* grammar, Node* from) {

	// functions to check if we can insert or delete on a given node
	// NOTE: the choice of which node is done uniformly within these functions
	std::function<int(const Node* n)> iable = [grammar](const Node* n){ return (int)(n->can_resample && grammar->replicating_Z(n->rule->nonterminal_type) > 0.0 ? 1 : 0); }; 
	std::function<int(const Node* n)> dable = [grammar](const Node* n){ return (int)(n->can_resample && n->rule->replicating_children() > 0 ? 1 : 0); }; 

	auto  ret = from->copy();
	int   N   = from->sum<int>(iable);
	
	if(N == 0) {
		return std::make_pair(ret, infinity); // TODO: THIS BREAKS DETAILED BALANCE I THINK?
	}
	
	int k = myrandom(N);
	Node* n = ret->get_nth(k, iable); // get a random kid
	
	double fb = 0.0;
	t_nonterminal t = n->rule->nonterminal_type;
	Rule* r = grammar->sample_replicating_rule(t); 
	size_t c = r->random_replicating_index(); // choose one
	
	
	Node* x = grammar->make<Node>(r);
	double olp = 0.0; // prob of everythign else 
	for(size_t i=0;i<r->N;i++){
		if(i == c) {
			x->child[i] = n->copy(); // extra copy needed here since n will take over x
		}
		else {
			x->child[i] = grammar->generate<Node>(r->child_types[i]);
			olp += grammar->log_probability(x->child[i]);
		}
	}
	
	// n now takes the resources of x, and deletes its own
	n->takeover(x); // this must come before computing fb, since it depends on the rest of the tree
	delete x; // what's left of it 
	
	double forward_choose  = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children()); // I could have put n in this many places
	double backward_choose = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
	
	fb += (-log(N) + (log(r->p)-log(grammar->replicating_Z(t))) + forward_choose + olp) 
		  // forward prob is choosing a node, choosing the rule, where it goes, and the other trees
		  -
		  // backward prob is choosing the node, any any equivalent child to promotef
		  (-log(ret->sum(dable)) + backward_choose );
//	CERR "\t"<< fb ENDL;
	
	return std::make_pair(ret, fb);		
}


//	
//	
std::pair<Node*, double> delete_proposal(Grammar* grammar, Node* from) {
	// choose a random node in n and delete
	// NOTE: we do NOT choose a deleteable node here, as that's a little harder to compute
	
	// ASSUME: value is a Node*
	
	std::function<int(const Node* n)> iable = [grammar](const Node* n){ return (int)(n->can_resample && grammar->replicating_Z(n->rule->nonterminal_type) > 0 ? 1 : 0); }; 
	std::function<int(const Node* n)> dable = [grammar](const Node* n){ return (int)(n->can_resample && n->rule->replicating_children() > 0 ? 1 : 0); }; 
	
	auto  ret = from->copy();
	int   N   = from->sum<int>(dable);
	
	if(N == 0) {
		return std::make_pair(ret, infinity); // TODO: THIS BREAKS DETAILED BALANCE I THINK?
	}
	
	int k = myrandom(N);
	Node* n = ret->get_nth(k, dable); // pick a node
	
	double fb = 0.0;
	t_nonterminal t = n->rule->nonterminal_type;
	assert(grammar->replicating_Z(t) > 0.0);
	size_t c = n->rule->random_replicating_index(); // choose a random replicating child (who therefore can be promoted to me)
	
	auto x = n->child[c]->copy(); // slightly more convenient to copy the kid
	
	double olp = 0.0; // compute the lp of generating the *other* kids
	for(size_t i=0;i<n->rule->N;i++) {
		if(i != c) 
			olp += grammar->log_probability(n->child[i]);
	}
	
	// probability of rule
	double rlp = log(n->rule->p) - log(grammar->replicating_Z(t));
	
	double forward_choose  = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
	double backward_choose = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
	
	n->takeover(x);	 // this must come after computing fb above for deleting
	delete x; // what's left of it 
	
	fb += (-log(N) + forward_choose) 
		  // forward prob is choosing a node, choosing any equivalent child to the one we got to promote
		  -
		  // backward prob is choosing the node, choosing the rule, choosing ANY equivalent child, and generating the other trees
		  (-log(ret->sum(iable)) + rlp + backward_choose + olp);
//	CERR "\t"<< fb ENDL;
	
	return std::make_pair(ret, fb);			
}




#endif