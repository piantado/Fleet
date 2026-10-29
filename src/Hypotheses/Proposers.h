#pragma once

//#define DEBUG_PROPOSE 1

#include <optional>
#include <utility>
#include <tuple>

#include "Node.h"

namespace Proposals { 
		
	/**
	 * @brief Helper function for whether we can resample from a node (just accesses n.can_resample)
	 * @param n - what node are we asking about?
	 * @return - a double (1.0 or 0.0) depending on whether n can be sampled
	 */
	double can_resample(const Node& n) {
		return n.can_resample*1.0;
	}
	
	template<typename GrammarType>
	std::optional<std::pair<Node,double>> prior_proposal(GrammarType* grammar, const Node& from) {
		auto g = grammar->generate(from.nt());
		return {g, grammar->log_probability(g) - grammar->log_probability(from)};
	}
	
	/**
	 * @brief Probability of proposing from a to b under regeneration
	 * @param grammar
	 * @param a
	 * @param b
	 * @return 
	 */	
	template<typename GrammarType>
	double p_regeneration_propose_to(GrammarType* grammar, const Node& a, const Node& b) {
		
		// TODO: Currently does not take into account can_resample
		// TODO: FIX THAT PLEASE
		
		// what's the probability of replacing the root of a and generating b?
		double alp = -log(a.count()); // probability of choosing any given node o in a
		double wholetree = alp + grammar->log_probability(b);
		
		if(a.rule != b.rule) {
			// I must regenerate this whole tree
			return wholetree;
		}
		else {
			assert(a.nchildren() == b.nchildren()); // not handling missing kids
			
			size_t ndiff = 0; // how many children are different?
			int who = 0; // if there is exactly one difference, who is it?
			for(size_t i = 0;i<a.nchildren();i++) {
				if(a.child(i) != b.child(i)) { 
					who = i;
					ndiff++;
				}
			}
			
			if(ndiff == 0) {
				// if all the kids are the same, we could propose to any of them...
				double lp = wholetree; // could propose to whole tree
				for(size_t i=0;i<a.nchildren();i++) {
					lp = logplusexp(lp, alp + p_regeneration_propose_to(grammar, a.child(i), b.child(i)));
				}
				return lp;
			}
			else if(ndiff == 1) {
				// we have to propose to who or root
				return logplusexp(wholetree, 
								  alp + p_regeneration_propose_to(grammar, a.child(who), b.child(who)));
			}
			else {
				// more than one difference means we have to propose to the root
				return wholetree; 
			}
				
		}
	}

//	/**
//	 * @brief A little helper function that resamples everything below when we can. If we can't, then we'll recurse
//	 * @param grammar
//	 * @param from
//	 */	
//	template<typename GrammarType>
//	void __regenerate_when_can_resample(const GrammarType* grammar, Node& from) {
//		if(from.can_resample) {
//			from.assign(grammar->generate(from.nt())); 
//		}
//		else { // we can't regenerate so we have to recurse
//			for(auto& c : from.get_children()) {
//				__regenerate_when_can_resample(grammar, c);
//			}			
//		}
//		
//	}

	/**
	 * @brief Regenerate with a rational-rules (Goodman et al.) style regeneration proposal: pick a node uniformly and regenerate it from the grammar. 
	 * @param grammar - what grammar to use
	 * @param from - what node are we proposing from
	 * @return A pair of the new proposed tree and the forward-backward log probability (for use in MCMC)
	 */
	template<typename GrammarType>
	std::optional<std::pair<Node,double>> regenerate(GrammarType* grammar, const Node& from) {
				
		// copy, regenerate a random node, and return that and forward-backward prob

		Node ret = from; // copy

		// Compute the normalizer once and reuse it (z==0 means nothing is resampleable). 
		// NOTE: we use the recursive BaseNode::sum rather than the iterator-based sample_z: 
		// the recursion inlines (see max-inline-insns-recursive in Fleet.mk) while the 
		// NodeIterator walks parent pointers with a function call per node, which showed up 
		// as a few percent of total MCMC time. The weights are exactly 0.0/1.0, so the sum 
		// is an exact integer in any summation order and the value is identical. 
		const double z = ret.sum<double>(+[](const Node& n) { return 1.0*n.can_resample;});
		if(z == 0.0)
			return {};

		// 9/2023 -- No ide what this note below was, or what that edit was.
			// We are changing this as of Feb 2022, we now can *choose* a can_resample Node, but
			// we won't actually change any unless can_resample is true
			// BUT if we DO change a can_resample node, we change everything below it.
		// I changed to sample only those we can resample:
		auto [s, slp] = sample<Node,Node>(ret, z, +[](const Node& n) { return 1.0*n.can_resample;});
		
//		#ifdef DEBUG_PROPOSE
//			DEBUG("REGENERATE", from, *s, s->can_resample);
//		#endif
			
		double oldgp = grammar->log_probability(*s); // reverse probability generating

		s->assign(grammar->generate(s->nt()));
		// also removed 9/2023:
//		__regenerate_when_can_resample(grammar,*s);

		// The backward move must choose s out of the NEW tree, which may have a different
		// number of (resampleable) nodes than the old one. (sum, not sample_z: see above.)
		const double z_new = ret.sum<double>(+[](const Node& n) { return 1.0*n.can_resample;});

		//double fb = slp + grammar->log_probability(*s)
		//		  - (log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp);
		// NOTE: the proposal above samples s uniformly among the nodes with can_resample set,
		// so the Metropolis-Hastings correction must use the number of *resampleable* nodes
		// (z and z_new), not the total node counts: using count() here (as this used to) is
		// identical when every node is resampleable, and wrong otherwise.
		double fb = (-log(z)     + grammar->log_probability(*s)) -
				    (-log(z_new) + oldgp);

		#ifdef DEBUG_PROPOSE
			CERR "FORWARD" TAB slp + grammar->log_probability(*s) ENDL;
			CERR "BACKWARD" TAB log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp ENDL;
			CERR "PROPOSING" TAB ret ENDL;
			CERR "----------------" ENDL;
			
		#endif

		return std::make_pair(std::move(ret), fb); // move: ret is a whole tree, don't deep-copy it into the pair
	}

	
	
	
	/**
	 * @brief Regenerate with rational-rules style proposals, but only allow proposals to trees
	 *        with a max depth of D. This encourages smaller changes, but also gets you stuck
	 *        pretty bad in local maxima since you can't take big hops. Probably will work 
	 *        best with restarts
	 * @param grammar
	 * @param from
	 * @return 
	 */
	template<typename GrammarType, int D>
	std::optional<std::pair<Node,double>> regenerate_shallow(GrammarType* grammar, const Node& from) {
		
		auto my_can_resample = +[](const Node& n) {
			return (n.can_resample and n.depth() <= D )*1.0;
		};
		
//		#ifdef DEBUG_PROPOSE
//			CERR "REGENERATE_SHALLOW" TAB from.string() ENDL;
//		#endif

		Node ret = from; // copy

		if(from.sum<double>(my_can_resample) == 0.0) 
			return {};
		
		auto [s, slp] = sample<Node,Node>(ret, my_can_resample);
		
		double oldgp = grammar->log_probability(*s); // reverse probability generating 
		
		s->assign(grammar->generate(s.first->nt())); 
		
		double fb = slp + grammar->log_probability(*s.first) 
				  - (log(my_can_resample(*s.first)) - log(ret.sum(my_can_resample)) + oldgp);

		return std::make_pair(ret, fb);
	}

	

	template<typename GrammarType>
	std::optional<std::pair<Node, double>> insert_tree(GrammarType* grammar, const Node& from) {
		// This proposal selects a node s, generates a replacement subtree t of the same type, 
		// and then re-inserts the old s at a randomly chosen slot of matching type inside t. 
		//
		// FORWARD-BACKWARD DERIVATION (pathwise, with the overwritten subtree as an auxiliary
		// variable -- this is what makes the correction exact; see Testing/DetailedBalance):
		//   forward path:  choose s uniformly among resampleable nodes       1/Zx
		//                  generate t                                        G(t)
		//                  choose the slot q among type-matching,            1/Zt
		//                  resampleable nodes of t
		//                  (the generated subtree u that sat at q is overwritten by old_s)
		//   reverse path (a delete_tree move): choose the same position      1/Zy
		//                  choose the old_s slot among type-matching         1/ZT
		//                  nodes of the final subtree T
		//                  draw its auxiliary subtree u' = u                 G(u)
		// so fb = [ -log Zx + lp(t) - log Zt ] - [ -log Zy + lp(u) - log ZT ],
		// and lp(t) - lp(u) = lp(T) - lp(old_s), which makes fb_insert(a->b) exactly equal to
		// -fb_delete(b->a) path by path. 
		//
		// NOTE: the old version used lp(t) - lp(old_s) for the generation term and symmetric
		// lp_sample_eq terms that cancelled, so its correction contained the log probability 
		// of the *discarded* subtree u -- a path-dependent error of many nats that measurably
		// broke detailed balance (see Testing/DetailedBalance). 
		
		Node ret = from; // copy

		const double Zx = ret.sum<double>(can_resample);
		if(Zx == 0.0) 
			return {};
		
		auto [s, slp] = sample<Node,Node>(ret, Zx, can_resample); // s is a ptr into ret; slp = -log Zx
		
		#ifdef DEBUG_PROPOSE
			DEBUG("INSERT-TREE", from, *s);
		#endif
		
		Node old_s = *s; // the old value of s, copied -- it gets pasted back in below
		
		Node* captured_s = s; // clang doesn't like taking s for some reason
		std::function can_resample_matches_s_nt = [captured_s](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == captured_s->nt()); 
		};
		
		// make something of the same type as s that we can put old_s into as a subtree below
		Node t = grammar->generate(s->nt()); 
		const double lp_t = grammar->log_probability(t);
		s->assign(std::move(t));
				
		// q is the slot in t that old_s will replace
		// (sampled after the assignment so q points into ret)
		auto [q, qlp] = sample<Node,Node>(*s, can_resample_matches_s_nt); // qlp = -log Zt
		
		const double lp_u = grammar->log_probability(*q); // the generated subtree we are about to overwrite
		
		// and then we assign the slot q to be the original s
		q->assign(std::move(old_s));
		
		const double logZT = log(s->template sum<double>(can_resample_matches_s_nt)); // matching slots in the final subtree
		const double Zy    = ret.sum<double>(can_resample);                  // resampleable nodes of the result
		
		const double forward  = slp + lp_t + qlp;
		const double backward = -log(Zy) + lp_u - logZT;
		
		assert(not std::isinf(forward));
		assert(not std::isinf(backward));
		
		return std::make_pair(std::move(ret), forward-backward); // move: don't deep-copy the tree
	}
	
	template<typename GrammarType>
	std::optional<std::pair<Node, double>> delete_tree(GrammarType* grammar, const Node& from) {
		// The reverse of insert_tree: select a node s, select a type-matching node q inside 
		// it, and promote q to replace s (deleting everything in s around q). 
		//
		// FORWARD-BACKWARD DERIVATION (see insert_tree): the reverse move is an insert_tree 
		// that regenerates old_s around q, i.e. generates t_rev = old_s with some subtree u' 
		// in place of q. u' is an auxiliary variable of the reverse path: its generation 
		// probability cancels (it appears in both lp(t_rev) and the forward side of the 
		// pairing), but the number of type-matching slots of t_rev -- the normalizer of the 
		// reverse slot choice -- depends on u', so we DRAW u' from the grammar and use its 
		// matching count. This makes fb_delete(a->b) exactly -fb_insert(b->a) path by path. 
		//   forward path:  choose s                                          1/Zx
		//                  choose q among type-matching nodes of s           1/Zsub
		//                  (draw the reverse move's auxiliary u'             G(u') -- cancels)
		//   reverse path:  choose the same position                          1/Zy
		//                  generate t_rev = old_s with u' at q's slot        G(old_s)/G(q)*G(u')
		//                  choose the slot among type-matching nodes         1/Ztrev,
		//                  of t_rev                                          Ztrev = Zsub - m(q) + m(u')

		Node ret = from; // copy

		const double Zx = ret.sum<double>(can_resample);
		if(Zx == 0.0) 
			return {};
		
		// s is who we edit at
		auto [s, slp] = sample<Node,Node>(ret, Zx, can_resample); // s is a ptr to ret; slp = -log Zx
		
		#ifdef DEBUG_PROPOSE
			DEBUG("DELETE-TREE", from, *s);
		#endif

		Node* captured_s = s; // clang doesn't like capturing s for some reason
		std::function can_resample_matches_s_nt = [captured_s](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == captured_s->nt()); 
		};
		
		const double lp_old_s = grammar->log_probability(*s);
		const double Zsub     = s->template sum<double>(can_resample_matches_s_nt);
		
		// q is who we promote here
		auto [q, qlp] = sample<Node,Node>(*s, Zsub, can_resample_matches_s_nt); // qlp = -log Zsub
		
		const double lp_q = grammar->log_probability(*q);
		const double m_q  = q->template sum<double>(can_resample_matches_s_nt); // matching slots inside the promoted subtree
		
		// the reverse insert's auxiliary subtree u' (only its matching count enters fb; 
		// its generation probability cancels between the two path directions)
		Node uprime = grammar->generate(s->nt());
		const double m_u = uprime.sum<double>(can_resample_matches_s_nt);
		
		Node newq = *q; // must make a copy since q lives inside what the assignment destroys
		
		// promote q here
		s->assign(std::move(newq));
	
		const double Zy     = ret.sum<double>(can_resample);
		const double logZtrev = log(Zsub - m_q + m_u);
		
		const double forward  = slp + qlp;
		const double backward = -log(Zy) + (lp_old_s - lp_q) - logZtrev;
		
		assert(not std::isinf(forward));
		assert(not std::isinf(backward));
		
		return std::make_pair(std::move(ret), forward-backward); // move: don't deep-copy the tree
	}
	
	
	/**
	 * @brief This samples functions f(a,b) -> g(a,b) (e.g. without destroying what's below). This uses a 
	 *        little trick that the node only stores the rule, so we can swap it out if we want
	 * @param grammar
	 * @param from
	 * @return 
	 */	
	template<typename GrammarType>
	std::optional<std::pair<Node,double>> sample_function_leaving_args(GrammarType* grammar, const Node& from) {
	
		// We add a restriction here that it must be a function (not a leaf)
		// since the leaves get well-done by regenration
		std::function allowed = +[](const Node& n) {
			return can_resample(n) and n.nchildren() > 0; 
		};
		
		Node ret = from; // copy
		
		auto z = sample_z<Node,Node>(ret, allowed);
		if(z == 0.0) return {};
		
		auto [s, slp] = sample<Node,Node>(ret, z, allowed);
		
		#ifdef DEBUG_PROPOSE
			print("SAMPLE-LEAVING-ARGS", from, *s);
		#endif
		
		// find everything in the grammar that matches s's type
		std::vector<Rule*> matching_rules;
		for(auto& r: grammar->rules[s->rule->nt]) {
			if(r.child_types == s->rule->child_types) {
				matching_rules.push_back(&r);
//				print("Matching ", r, *s->rule);
			}
		}
		assert(matching_rules.size() >= 1); // we had to have matchd one...
		if(matching_rules.size() == 1) { // don't do this proposal if there is only one rule
			return {};
		}
		
		// now sample from one
		std::function sampler = +[](const Rule* r) -> double { return r->p; };
		auto [newr, __rp] = sample<Rule*>(matching_rules, sampler); // sample according to p
		assert(newr != nullptr and *newr != nullptr);
		assert( (*newr)->nt == s->rule->nt);
		// NOTE: confusingly, newr is a Rule** so it must be deferenced to get a poitner 
		const Rule* oldRule = s->rule; 
		
		// set the rule and its probability -- save us the copying
		s->rule = *newr;
		s->lp = (*newr)->logp - grammar->logZ[s->rule->nt]; // a LOG probability, as makeNode sets (this used to store p/Z)
		
		// here we compute fb while ignoring the normalizing constants which is why we don't use rp
		double fb = log(sampler(*newr))-log(sampler(oldRule));
		
		return std::make_pair(std::move(ret),fb); // forward-backward is just probability of sampling rp since s cancels
	}


	/**
	 * @brief This propose swaps around arguments of the same type
	 * @param grammar
	 * @param from
	 * @return 
	 */
	template<typename GrammarType>
	std::optional<std::pair<Node,double>> swap_args(GrammarType* grammar, const Node& from) {
		
		Node ret = from; // copy
		
		auto z = sample_z<Node,Node>(ret, can_resample);
		if(z == 0.0) return {};
		
		auto [s, slp] = sample<Node,Node>(ret, z, can_resample);
		if(s->nchildren() <= 1) { 
			return {};
		}
			
		auto N = s->nchildren();
		
		// go through and find children of the same type
		// this is a bit inefficient but the ns here are very small typically
		std::vector<int> possible_indices; // for swapping
		for(size_t i=0;i<N;i++) {
			for(size_t j=i+1;j<N;j++) {
				if(s->child(j).rule->nt == s->child(i).rule->nt) { // if there is something else with the same type, we can swap 
					possible_indices.push_back(i);
					break; 
				}
			}
		}
		if(possible_indices.size() == 0) return {}; 

		
//		#ifdef DEBUG_PROPOSE
//			DEBUG("SWAP-ARGS", from, *s);
//		#endif
		
		// Sample one we can swap 
		auto x = possible_indices.at(sample_int(possible_indices.size()).first);
		auto y = sample_int(N, [&](const int v) { return 1*(s->child(v).rule->nt == s->child(x).rule->nt and v != x); }).first;
			
		Node tmp = s->child(x); // copy (unfortunately, though maybe it won't be necessary if we use std::swap)
		s->set_child(x, std::move(s->child(y)));
		s->set_child(y, std::move(tmp));
//		print("SWAPPED:", ret);

		return std::make_pair(std::move(ret),0.0);
	}
		
		

}