#pragma once


#include <utility>
#include <tuple>

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
	 * @brief Regenerate with a rational-rules (Goodman et al.) style regeneration proposal: pick a node uniformly and regenerate it from the grammar. 
	 * @param grammar - what grammar to use
	 * @param from - what node are we proposing from
	 * @return A pair of the new proposed tree and the forward-backward log probability (for use in MCMC)
	 */
	template<typename GrammarType>
	std::pair<Node,double> regenerate(GrammarType* grammar, const Node& from) {
				
		// copy, regenerate a random node, and return that and forward-backward prob
		#ifdef DEBUG_MCMC
			CERR "REGENERATE" TAB from.string() ENDL;
		#endif

		Node ret = from; // copy

		if(from.sum<double>(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		auto [s, slp] = sample<Node,Node>(ret, can_resample);
		
		double oldgp = grammar->log_probability(*s); // reverse probability generating 
		
		s->assign(grammar->generate(s->nt())); 
		
		double fb = slp + grammar->log_probability(*s) 
				  - (log(can_resample(*s)) - log(ret.sum(can_resample)) + oldgp);

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
		
//		#ifdef DEBUG_MCMC
			CERR "REGENERATE_SHALLOW" TAB from.string() ENDL;
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
		
		#ifdef DEBUG_MCMC
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
		
		// the old value of s, copied -- needed for fb and for replacement
		Node old_s = *s; 
		std::function can_resample_matches_s_nt = [=](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == s->nt()); 
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
		auto lpq = lp_sample_eq(*s, t, can_resample_matches_s_nt); // 
			
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
		
		return std::make_pair(ret, forward-backward);		
	}
	
	template<typename GrammarType>
	std::pair<Node, double> delete_tree(GrammarType* grammar, const Node& from) {
		// This proposal selects a node, regenerates, and then copies what was there before somewhere below 
		// in the replaced tree. NOTE: it must regenerate something with the right nonterminal
		// since that's what's being replaced! 
		
		#ifdef DEBUG_MCMC
			CERR "DELETE-TREE"  TAB from.string()  ENDL;
		#endif

		Node ret = from; // copy

		if(ret.sum(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		// s is who we edit at
		auto [s, slp] = sample<Node,Node>(ret, can_resample); // s is a ptr to ret
		Node old_s = *s; // the old value of s, copied -- needed for fb

		std::function can_resample_matches_s_nt = [&](const Node& n) -> double { 
			return can_resample(n)*(n.nt() == s->nt()); 
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
		
		return std::make_pair(ret, forward-backward);		
	}
	
	
		

}