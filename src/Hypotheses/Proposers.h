#pragma once

//#define DEBUG_PROPOSE 1

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
	std::pair<Node,double> prior_proposal(GrammarType* grammar, const Node& from) {
		auto g = grammar->generate(from.nt());
		return std::make_pair(g, grammar->log_probability(g) - grammar->log_probability(from));
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

	/**
	 * @brief Regenerate with a rational-rules (Goodman et al.) style regeneration proposal: pick a node uniformly and regenerate it from the grammar. 
	 * @param grammar - what grammar to use
	 * @param from - what node are we proposing from
	 * @return A pair of the new proposed tree and the forward-backward log probability (for use in MCMC)
	 */
	template<typename GrammarType>
	std::pair<Node,double> regenerate(GrammarType* grammar, const Node& from) {
				
		// copy, regenerate a random node, and return that and forward-backward prob
		#ifdef DEBUG_PROPOSE
			CERR "REGENERATE" TAB from.string() ENDL;
		#endif

		Node ret = from; // copy

		if(from.sum<double>(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		auto [s, slp] = sample<Node,Node>(ret, can_resample);
		
		#ifdef DEBUG_PROPOSE
		CERR "PROPOSING AT " TAB (*s) ENDL;
		#endif
			
		double oldgp = grammar->log_probability(*s); // reverse probability generating 
		
		s->assign(grammar->generate(s->nt())); 
		
		double fb = slp + grammar->log_probability(*s) 
				  - (log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp);

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
	std::pair<Node,double> regenerate_shallow(GrammarType* grammar, const Node& from) {
		
		auto my_can_resample = +[](const Node& n) {
			return (n.can_resample and n.depth() <= D )*1.0;
		};
		
//		#ifdef DEBUG_PROPOSE
//			CERR "REGENERATE_SHALLOW" TAB from.string() ENDL;
//		#endif

		Node ret = from; // copy

		if(from.sum<double>(my_can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		auto [s, slp] = sample<Node,Node>(ret, my_can_resample);
		
		double oldgp = grammar->log_probability(*s); // reverse probability generating 
		
		s->assign(grammar->generate(s.first->nt())); 
		
		double fb = slp + grammar->log_probability(*s.first) 
				  - (log(my_can_resample(*s.first)) - log(ret.sum(my_can_resample)) + oldgp);

		return std::make_pair(ret, fb);
	}

	

	template<typename GrammarType>
	std::pair<Node, double> insert_tree(GrammarType* grammar, const Node& from) {
		// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
		// in the replaced tree. NOTE: it must regenerate something with the right nonterminal
		// since that's what's being replaced! 
		
		#ifdef DEBUG_PROPOSE
			CERR "INSERT-TREE"  TAB from.string()  ENDL;
		#endif

		Node ret = from; // copy

		if(ret.sum<double>(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		// So: 
		// we pick node s to be replaced.
		// we create t to replace it with
		// then somewhere below t, we choose something of type s.nt(), called q, to put s
		
		auto [s, slp] = sample<Node,Node>(ret, can_resample); // s is a ptr into ret
//		PRINTN("Choosing s=", s->string());
		
		Node old_s = *s; // the old value of s, copied -- needed for fb and for replacement
		
		Node* captured_s = s; // clang doesn't like taking s for some reason
		std::function can_resample_matches_s_nt = [captured_s](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == captured_s->nt()); 
		};
		
		// make something of the same type as s that we can 
		// put s into as a subtree below
		Node t = grammar->generate(s->nt()); 
//		PRINTN("Generated t=", t.string());
		s->assign(t);// copy, not move, since we need it below
//		s->fullprint();
//		checkNode(grammar, *s);
				
		// q is the thing in t that s will replace
		// this is sampeld from t, but we do it from s after assignment
		// since that was a bug before ught
		auto [q, qlp] = sample<Node,Node>(*s, can_resample_matches_s_nt); 
//		PRINTN("Choosing q=", q->string());
		
		// and then we assign the subtree, q, to be the original s
		q->assign(old_s);
//		PRINTN("Ret after Q assignment=", ret.string());
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
//		PRINTN("RETURNINGI", ret);
		
//		if(std::isinf(forward)) {
//			PRINTN(slp, lpq, grammar->log_probability(t), grammar->log_probability(old_s) );
//			PRINTN(s->string(), t.string());
//			
//		}
		
		assert(not std::isinf(forward));
		assert(not std::isinf(backward));
		
		
		return std::make_pair(ret, forward-backward);		
	}
	
	template<typename GrammarType>
	std::pair<Node, double> delete_tree(GrammarType* grammar, const Node& from) {
		// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
		// in the replaced tree. NOTE: it must regenerate something with the right nonterminal
		// since that's what's being replaced
		
		#ifdef DEBUG_PROPOSE
			CERR "DELETE-TREE"  TAB from.string()  ENDL;
		#endif

		Node ret = from; // copy

		if(ret.sum(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		// s is who we edit at
		auto [s, slp] = sample<Node,Node>(ret, can_resample); // s is a ptr to ret
		Node old_s = *s; // the old value of s, copied -- needed for fb

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
	std::pair<Node,double> sample_function_leaving_args(GrammarType* grammar, const Node& from) {
		
		Node ret = from; // copy
		
		// TODO CHECK HERE THAT THE SAMPLING WAS POSSIBLE
		auto [s, slp] = sample<Node,Node>(ret, can_resample);
		
		// find everything in the grammar that matches s's type
		std::vector<Rule*> matching_rules;
		for(auto& r: grammar->rules[s->rule->nt]) {
			if(r.child_types == s->rule->child_types) {
				matching_rules.push_back(&r);
//				PRINTN("Matching ", r, *s->rule);
			}
		}
		assert(matching_rules.size() >= 1); // we had to have matche done...
		
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
		
		return std::make_pair(ret, fb); // forward-backward is just probability of sampling rp since s cancels
	}


	/**
	 * @brief This propose swaps around arguments of the same type
	 * @param grammar
	 * @param from
	 * @return 
	 */
	template<typename GrammarType>
	std::pair<Node,double> swap_args(GrammarType* grammar, const Node& from) {
		
		Node ret = from; // copy
//		PRINTN("Swapping1 ", ret);
		
		// TODO CHECK HERE THAT THE SAMPLING WAS POSSIBLE
		auto [s, slp] = sample<Node,Node>(ret, can_resample);
		
		// who is going to be swapped
		size_t J = myrandom(s->nchildren()); // check if anything is of this type
		
		// with what
		std::vector<int> matching_indices;
		for(size_t i=0;i<s->nchildren();i++) {
			if(i == J) 
				continue;
				
			if(s->child(i).rule->nt == s->child(J).rule->nt) {
				matching_indices.push_back(i);
			}
		}
		
		if(matching_indices.size() == 0) {
			
			return std::make_pair(ret, 0.0);
			
		}
		else {
			
			// choose one
			auto i = matching_indices[myrandom(matching_indices.size())];
			
			Node tmp = s->child(i); // copy (unfortunatley -- probably not necessary using std::swap)
			s->set_child(i, std::move(s->child(J)));
			s->set_child(J, std::move(tmp));
			
//			PRINTN("Swapping2 ", ret);
		
			return std::make_pair(ret, 0.0);
		}
	}
		
		

}