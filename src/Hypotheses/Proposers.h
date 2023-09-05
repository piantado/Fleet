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

		if(from.sum<double>(can_resample) == 0.0) 
			return {};
		
		// 9/2023 -- No ide what this note below was, or what that edit was. 
			// We are changing this as of Feb 2022, we now can *choose* a can_resample Node, but
			// we won't actually change any unless can_resample is true
			// BUT if we DO change a can_resample node, we change everything below it. 
		// I changed to sample only those we can resample:
		auto [s, slp] = sample<Node,Node>(ret, +[](const Node& n) { return 1.0*n.can_resample;});
		
//		#ifdef DEBUG_PROPOSE
//			DEBUG("REGENERATE", from, *s, s->can_resample);
//		#endif
			
		double oldgp = grammar->log_probability(*s); // reverse probability generating 

		s->assign(grammar->generate(s->nt())); 
		// also removed 9/2023:
//		__regenerate_when_can_resample(grammar,*s);
		
		//double fb = slp + grammar->log_probability(*s) 
		//		  - (log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp);
		double fb = (-log(from.count()) + grammar->log_probability(*s)) - 
				    (-log(ret.count()) + oldgp);

		#ifdef DEBUG_PROPOSE
			CERR "FORWARD" TAB slp + grammar->log_probability(*s) ENDL;
			CERR "BACKWARD" TAB log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp ENDL;
			CERR "PROPOSING" TAB ret ENDL;
			CERR "----------------" ENDL;
			
		#endif

		return std::make_pair(ret, fb);
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
		// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
		// in the replaced tree. NOTE: it must regenerate something with the right nonterminal
		// since that's what's being replaced! 
		
		Node ret = from; // copy

		if(ret.sum<double>(can_resample) == 0.0) 
			return {};
		
		// So: 
		// we pick node s to be replaced.
		// we create t to replace it with
		// then somewhere below t, we choose something of type s.nt(), called q, to put s
		
		auto [s, slp] = sample<Node,Node>(ret, can_resample); // s is a ptr into ret
//		print("Choosing s=", s->string());
		
		#ifdef DEBUG_PROPOSE
			DEBUG("INSERT-TREE", from, *s);
		#endif

		
		Node old_s = *s; // the old value of s, copied -- needed for fb and for replacement
		
		Node* captured_s = s; // clang doesn't like taking s for some reason
		std::function can_resample_matches_s_nt = [captured_s](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == captured_s->nt()); 
		};
		
		// make something of the same type as s that we can 
		// put s into as a subtree below
		Node t = grammar->generate(s->nt()); 
//		print("Generated t=", t.string());
		s->assign(t);// copy, not move, since we need it below
//		s->fullprint();
//		checkNode(grammar, *s);
				
		// q is the thing in t that s will replace
		// this is sampeld from t, but we do it from s after assignment
		// since that was a bug before ught
		auto [q, qlp] = sample<Node,Node>(*s, can_resample_matches_s_nt); 
//		print("Choosing q=", q->string());
		
		// and then we assign the subtree, q, to be the original s
		q->assign(old_s);
//		print("Ret after Q assignment=", ret.string());
//		s->fullprint();
		
//		checkNode(grammar, *s);

		// now if we replace something below t with s, there are multiples ones we could have done...
		auto lpq = lp_sample_eq(*q, *s, can_resample_matches_s_nt); // 
			
//		CERR "----INSERT-----------" ENDL;
//		CERR from ENDL;
//		CERR *s ENDL;
//		CERR t ENDL;
//		CERR s.second TAB grammar->log_probability(t) TAB grammar->log_probability(old_s) TAB lpq ENDL;
		
		// forward is choosing s, generating everything *except* what replaced s, and then replacing
		double forward = slp +  // must get exactly this s
						 (grammar->log_probability(t)-grammar->log_probability(old_s)) +  // generate the rest of the tree
						 lpq; // probability of getting any s
		
		/// backward is we choose t exactly, then we pick anything below that is equal to s
		double backward = lp_sample_one<Node,Node>(t, ret, can_resample) + 
						  lp_sample_eq<Node,Node>(old_s, *s, can_resample_matches_s_nt);
//		print("RETURNINGI", ret);
		
//		if(std::isinf(forward)) {
//			print(slp, lpq, grammar->log_probability(t), grammar->log_probability(old_s) );
//			print(s->string(), t.string());
//			
//		}
		
		assert(not std::isinf(forward));
		assert(not std::isinf(backward));
		
		return std::make_pair(ret, forward-backward);
	}
	
	template<typename GrammarType>
	std::optional<std::pair<Node, double>> delete_tree(GrammarType* grammar, const Node& from) {
		// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
		// in the replaced tree. NOTE: it must regenerate something with the right nonterminal
		// since that's what's being replaced

		Node ret = from; // copy

		if(ret.sum(can_resample) == 0.0) 
			return {};
		
		// s is who we edit at
		auto [s, slp] = sample<Node,Node>(ret, can_resample); // s is a ptr to ret
		Node old_s = *s; // the old value of s, copied -- needed for fb
		
		#ifdef DEBUG_PROPOSE
			DEBUG("DELETE-TREE", from, *s);
		#endif

		Node* captured_s = s; // clang doesn't like capturing s for some reason
		std::function can_resample_matches_s_nt = [&](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == captured_s->nt()); 
		};
		
		// q is who we promote here
		auto [q, qlp] = sample(*s, can_resample_matches_s_nt);
		Node newq = *q; // must make a copy since q gets deleted in assignment... TODO: Can clean up with std::move?
		
		// forward is choosing s, and then anything equal to q within
		double forward = slp + lp_sample_eq<Node,Node>(*q, *s, can_resample_matches_s_nt); 
		
		// probability of generating everything in s except q
		double tlp = grammar->log_probability(old_s) - grammar->log_probability(*q); 
		
		// promote q here
		s->assign(newq);
	
		/// backward is we choose the *new* s, then generate everything else, and choose anything equal
		double backward = lp_sample_one<Node,Node>(*s,ret,can_resample) + 
						  tlp + 
						  lp_sample_eq<Node,Node>(newq,old_s,can_resample_matches_s_nt);
		
		assert(not std::isinf(forward));
		assert(not std::isinf(backward));
		
		return std::make_pair(ret, forward-backward);
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
		s->lp = (*newr)->p / grammar->Z[s->rule->nt];
		
		// here we compute fb while ignoring the normalizing constants which is why we don't use rp
		double fb = log(sampler(*newr))-log(sampler(oldRule));
		
		return std::make_pair(ret,fb); // forward-backward is just probability of sampling rp since s cancels
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

		return std::make_pair(ret,0.0);
	}
		
		

}