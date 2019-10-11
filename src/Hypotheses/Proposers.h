#pragma once

// TODO: The insert_tree proposer is hard because we could potentially choose from a bunch of different g subtrees
//       Perhaps there is an aux variable argument to help us out?
//			Suppose we imagine placing two markers, A and B, on the tree uniformly at random. 
// 			When A dominates B, then we promote B to A. This happens with a probability that depends on the relative size below A
//			When A=B then we insert, this happens with a probability proportional to the tree size squared. 
//			Otherwise nothing happens. 
//			-- Seems like we must have violations of detailed balance with current scheme because we could have multiple possible
// 				insertions, like in not(not(not(...)))

#include <utility>
#include <tuple>

// TOOD: We could do insert/delete with entire trees -- replace any tree down below?
//double lpsample(const Node& t, const Node& n, std::function<int(const Node&)> choose) {
//	// probability of choosing n out of all in t
//	// NOTE: n need not be in t -- we just assume sampling is done uniformly if choose(n)
//	if(choose(n)) return -log(t.sum(choose));
//	else          return -infinity;	
//}
//double lpsample_eq(const Node& t, const Node& n, std::function<int(const Node&)> choose) {
//	// probability of choosing anything equal to n out of t 
//	if(choose(n)) return  log(t.count_equal(n))-log(t.sum(choose));
//	else          return -infinity;	
//}



std::pair<Node,double> regeneration_proposal(Grammar* grammar, const Node& from) {
	// copy, regenerate a random node, and return that and forward-backward prob
#ifdef DEBUG_MCMC
CERR "REGENERATE" ENDL;
#endif

	std::function<double(const Node& n)> can_resample = [](const Node& n) { return n.can_resample*1;};
			
	Node ret = from; // copy

	if(from.sum<double>(can_resample) == 0.0) {
		return std::make_pair(ret, 0.0);
	}
	
	//auto s = ret.sample(can_resample); // get the nth in ret
	auto s = sample<Node,Node>(ret, can_resample);
	
	double oldgp = grammar->log_probability(*s.first); // reverse probability generating 
	
	if(s.first->parent == nullptr) {
		*s.first = grammar->generate(s.first->rule->nt); // make something new of the same type
	}
	else {
		s.first->parent->set_child(s.first->pi, grammar->generate(s.first->rule->nt)); 
	}
//	*s.first = grammar->generate<Node>(s.first->rule->nt); // make something new of the same type
	
	double fb = s.second + grammar->log_probability(*s.first) 
			  - (log(can_resample(*s.first)) - log(ret.sum(can_resample)) + oldgp);
	
//	CERR fb ENDL;
//	CERR from.string() ENDL;
//	CERR ret.string() ENDL;
	return std::make_pair(ret, fb);
}


//
//
//std::pair<Node*, double> insert_proposal_tree(Grammar* grammar, Node* from) {
//	// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
//	// in the replaced tree 
//	
//	#ifdef DEBUG_MCMC
//	CERR "INSERT-TREE" ENDL;
//	#endif
//
//	std::function<int(const Node* n)> can_resample = [](const Node* n) { return n->can_resample*1;};
//			
//	auto ret = from->copy();
//	
//	if(from->sum(can_resample) == 0) 
//		return std::make_pair(ret, 0.0);
//	
//	Node* n = ret->sample(can_resample); // get the nth in ret		
//	Node* g = grammar->generate<Node>(n->rule->nt); // make something new of the same type 
//	
//	// find something of the right type (don't check can_resample?)
//	std::function<int(const Node* n)> right_type = [=](const Node* x) { return (int)(n->rule->nt==x->rule->nt);};
//	if(g->sum(right_type) == 0) 
//		return std::make_pair(ret, 0.0);
//	
//	// from dominates n; g dominates x
//	
//	Node* x = g->sample(right_type);  // x is the node that gets replaced with n 
//	x->takeover(n->copy()); // hmm should be able to do this without copy, but its hard to think about 
//	
//	double fb = 0.0;
//	fb += lpsample(ret,n,can_resample);
//	fb += grammar->log_probability(g) - grammar->log_probability(n); // probability of generating everything except n
//	fb += lpsample_eq(g,x,right_type); // probability of choosing x or anything equal out of all with right type
// 
//	// this adds up all of the ways we could have gotten something equal to n, summing over all possible g
//	std::function<double(const Node*)> s = [=](const Node* gg) { 
//				return can_resample(gg) ? lpsample(ret, gg, can_resample) + lpsample_eq(gg,n,right_type) : -infinity;
//	};
//	fb -= ret->logsumexp(s);
//	
//	n->takeover(g);
//	
//	return std::make_pair(ret, fb);		
//}
//
//std::pair<Node*, double> delete_proposal_tree(Grammar* grammar, Node* from) {
//	// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
//	// in the replaced tree 
//	
//	#ifdef DEBUG_MCMC
//	CERR "DELETE-TREE" ENDL;
//	#endif
//
//	std::function<int(const Node* n)> can_resample = [](const Node* n) { return n->can_resample*1;};
//			
//	auto ret = from->copy();
//
//	if(from->sum(can_resample) == 0) 
//		return std::make_pair(ret, 0.0);
//	
//	Node* g = ret->sample(can_resample); // pick a random node	
//	
//	// find something of the right type (don't check can_resample?)
//	std::function<int(const Node*)> right_type = [=](const Node* z) { return (int)(g->rule->nt==z->rule->nt);};
//	if(g->sum(right_type) == 0) 
//		return std::make_pair(ret, 0.0);
//	
//	Node* n = g->sample(right_type);  // pick a subtree
//
//	// add up all of the ways we could have gotten n
//	double fb = 0.0;
//	std::function<double(const Node*)> s = [=](const Node* gg) { 
//				return can_resample(gg) ? lpsample(ret, gg, can_resample) + lpsample_eq(gg,n,right_type) : -infinity;
//	};
//	fb += ret->logsumexp(s);
//
//	
//	fb -= grammar->log_probability(g) - grammar->log_probability(n); // generate everything but n 
//	
//	g->takeover(n->copy()); // hmm should be able to do this without copy, but its hard to think about, because Nodes will recursively destruct
//	
//	fb -= lpsample(ret, g, can_resample); // pick that g out of ret
//	fb -= lpsample_eq(g, n, right_type); // backwards we sample g, then n from g
//	
//	return std::make_pair(ret, fb);		
//}
//
//
//
//
//
//std::pair<Node*, double> insert_proposal(Grammar* grammar, Node* from) {
//#ifdef DEBUG_MCMC
//CERR "INSERT" ENDL;
//#endif
//	// functions to check if we can insert or delete on a given node
//	// NOTE: the choice of which node is done uniformly within these functions
//	std::function<int(const Node* n)> iable = [grammar](const Node* n){ return (int)(n->can_resample && grammar->replicating_Z(n->rule->nt) > 0 ? 1 : 0); }; 
//	std::function<int(const Node* n)> dable = [grammar](const Node* n){ return (int)(n->can_resample && n->rule->replicating_children() > 0 ? 1 : 0); }; 
//
//	auto  ret = from->copy();
//	int   N   = from->sum<int>(iable);
//	
//	if(N == 0) {
//		return std::make_pair(ret, infinity); // TODO: THIS BREAKS DETAILED BALANCE I THINK?
//	}
//	
//	int k = myrandom(N);
//	Node* n = ret->get_nth(k, iable); // get a random kid
//	
//	double fb = 0.0;
//	nonterminal_t t = n->rule->nt;
//	Rule* r = grammar->sample_replicating_rule(t); 
//	size_t c = r->random_replicating_index(); // choose one
//	
//	
//	Node* x = grammar->make<Node>(r);
//	double olp = 0.0; // prob of everythign else 
//	for(size_t i=0;i<r->N;i++){
//		if(i == c) {
//			x->child[i] = n->copy(); // extra copy needed here since n will take over x
//		}
//		else {
//			x->child[i] = grammar->generate<Node>(r->child_types[i]);
//			olp += grammar->log_probability(x->child[i]);
//		}
//	}
//	
//	// n now takes the resources of x, and deletes its own
//	n->takeover(x); // this must come before computing fb, since it depends on the rest of the tree
//	
//	double forward_choose  = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children()); // I could have put n in this many places
//	double backward_choose = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
//	
//	fb += (-log(N) + (log(r->p)-log(grammar->replicating_Z(t))) + forward_choose + olp) 
//		  // forward prob is choosing a node, choosing the rule, where it goes, and the other trees
//		  -
//		  // backward prob is choosing the node, any any equivalent child to promotef
//		  (-log(ret->sum(dable)) + backward_choose );
////	CERR "\t"<< fb ENDL;
//	
//	return std::make_pair(ret, fb);		
//}
//
//
////	
////	
//std::pair<Node*, double> delete_proposal(Grammar* grammar, Node* from) {
//	// choose a random node in n and delete
//	// NOTE: we do NOT choose a deleteable node here, as that's a little harder to compute
//	
//	// ASSUME: value is a Node*
//#ifdef DEBUG_MCMC
//CERR "DELETE" ENDL;
//#endif 
//
//	std::function<int(const Node* n)> iable = [grammar](const Node* n){ return (int)(n->can_resample && grammar->replicating_Z(n->rule->nt) > 0 ? 1 : 0); }; 
//	std::function<int(const Node* n)> dable = [grammar](const Node* n){ return (int)(n->can_resample && n->rule->replicating_children() > 0 ? 1 : 0); }; 
//	
//	auto  ret = from->copy();
//	int   N   = from->sum<int>(dable);
//	
//	if(N == 0) {
//		return std::make_pair(ret, infinity); // TODO: THIS BREAKS DETAILED BALANCE I THINK?
//	}
//	
//	int k = myrandom(N);
//	Node* n = ret->get_nth(k, dable); // pick a node
//	
//	double fb = 0.0;
//	nonterminal_t t = n->rule->nt;
//	assert(grammar->replicating_Z(t) > 0.0);
//	size_t c = n->rule->random_replicating_index(); // choose a random replicating child (who therefore can be promoted to me)
//	
//	auto x = n->child[c]->copy(); // slightly more convenient to copy the kid
//	
//	double olp = 0.0; // compute the lp of generating the *other* kids
//	for(size_t i=0;i<n->rule->N;i++) {
//		if(i != c) 
//			olp += grammar->log_probability(n->child[i]);
//	}
//	
//	// probability of rule
//	double rlp = log(n->rule->p) - log(grammar->replicating_Z(t));
//	
//	double forward_choose  = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
//	double backward_choose = log(n->count_equal_child(n->child[c])) - log(n->rule->replicating_children());
//	
//	n->takeover(x);	 // this must come after computing fb above for deleting
//	
//	fb += (-log(N) + forward_choose) 
//		  // forward prob is choosing a node, choosing any equivalent child to the one we got to promote
//		  -
//		  // backward prob is choosing the node, choosing the rule, choosing ANY equivalent child, and generating the other trees
//		  (-log(ret->sum(iable)) + rlp + backward_choose + olp);
////	CERR "\t"<< fb ENDL;
//	
//	return std::make_pair(ret, fb);			
//}
//
//
//template<typename HYP, typename T, nonterminal_t targetnt> // takes a hypothesis type, an inner hypothesis T, and a nonterminal type
//std::pair<HYP*, double> inverseInliningProposal(Grammar* grammar, const HYP* h) {
//	// TODO: May not be right if we extract something with a recursive call?
//	
//	assert(h->factors.size() > 0);
//	std::function<double(const Node* n)> is_right_type = [](const Node* n) { return 1.0*(n->rule->nt == targetnt);};
//	
//	HYP* ret = h->copy(); 
//	double forward = -infinity, backward = -infinity;;
//	
//	// TODO: THESE IF STATEMENTS MESS UP F/B
//	if((flip()||h->factors.size()==1) && h->factors.size() < HYP::MAX_FACTORS-2){ // extract subtree to new factor
//		ret->fix_indices(0,1); // fix the indices for everything in ret if we inset 1 at position 0
//		
//		// now find a factor
//		size_t l   = h->factors.size();
//		size_t fi  = myrandom(l);
//		Node* t    = h->factors[fi]->value->sample(is_right_type)->copy();
//		
//		// now we need a node for calling that
//		Node* call = grammar->expand_from_names<Node>(std::string("F:0:x")); // call with x so x gets bound the right way in calls; 0 bc we put in front
//		
//		// to replace, we will extract each one that we see
//		std::function<bool(Node* n)> f = [call, t](Node* n) {
//			if(*n == *t) {
//				n->takeover(call->copy());
//				return false;
//			}
//			return true;
//		};
//		for(auto fac: ret->factors){
//			fac->value->map_conditionalrecurse(f); // replacing function mapped through nodes				
//		}
//		
//		// and add at the front so that if we are calling the last function, we don't mess anyhting up
//		ret->factors.insert(ret->factors.begin(), new T(grammar, t));
//		
//		
//		// forward probability is prob of choosing each factor times the prob of choosing 
//		// anything equal to t within them
//		
//		for(auto f : h->factors) {
//			auto cnt = f->value->count_equal(t);
//			if(cnt > 0) 
//				forward = logplusexp(forward, -log(l+1) + log(cnt) - log(f->value->count()));
//		}
//		
//		size_t cb = 0;
//		for(auto f: ret->factors) {
//			if(*f->value == *t) cb++;
//		}
//		backward = log(cb) - log(ret->factors.size());
//		
//		delete call;
////			CERR forward TAB backward TAB cb TAB ret->factors.size() ENDL;
//		
//	} 
//	else if(h->factors.size() > 1) { // NOTE: for now this only works on calls with F(x) -- we could try to do it so we can substitute what x was
//			// TODO: Fix detailed balance from this if statement
//		
//		//CERR "REMOVE" ENDL;
//		
//		size_t l  = h->factors.size();
//		size_t fi = myrandom(l); // this one is going to be deleted
//		Node*  t  = ret->factors[fi]->value->copy();			
//		
//		Node* call = grammar->expand_from_names<Node>(std::string("F:"+std::to_string(fi)+":x")); // call with x so x gets bound the right way in calls
//		
//		std::function<bool(Node* n)> f = [call, t](Node* n) {
//			if(*n == *call) {
//				n->takeover(t->copy());
//				return false;
//			}
//			return true;
//		};
//		
//		for(size_t i=0;i<ret->factors.size();i++) {
//			if(i != fi) ret->factors[i]->value->map_conditionalrecurse(f);
//		}
//		
//		// and we can remove that factor
//		// also deletes t
//		ret->factors.erase(ret->factors.begin()+fi); 
//					
//		// we are going to remove position fi, so we have to fix anything that referneces it and higher
//		ret->fix_indices(fi,-1); 
//					
//		size_t cb = 0;
//		for(auto f: h->factors) {
//			if(*f->value == *call) cb++;
//		}
//		forward = log(cb) - log(h->factors.size());
//		
//		for(auto f : ret->factors) {
//			auto cnt = f->value->count_equal(t);
//			if(cnt > 0) 
//				backward = logplusexp(backward, -log(l) + log(cnt) - log(f->value->count()));
//		}
//		
//		delete call;
//	}
//	
//	return std::make_pair(ret,forward-backward);
//}
