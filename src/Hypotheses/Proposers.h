#pragma once

/* NOTE: The insert/delete detailed balance have not been checked done yet 
 * 
 * 
 * There's a slight problem here, which is that we often have many ways we could have inserted, corresponding to
 * all of the ways we can pick a subtree of s (e.g. summing over that)
 * E.g. when we go backwards from a delete, when there are multiple identical subnodes, we can have many ways....
 * */

#include <utility>
#include <tuple>

namespace Proposals { 
		
	double can_resample(const Node& n) {
		/**
		 * @brief Helper function for whether we can resample from a node (just accesses n.can_resample)
		 * @param n - what node are we asking about?
		 * @return - a double (1.0 or 0.0) depending on whether n can be sampled
		 */
		return n.can_resample*1.0;
	}
	
	std::pair<Node,double> prior_proposal(Grammar* grammar, const Node& from) {
		auto g = grammar->generate(from.nt());
		return std::make_pair(g, grammar->log_probability(g) - grammar->log_probability(from));
	}

	std::pair<Node,double> regenerate(Grammar* grammar, const Node& from) {
		/**
		 * @brief Regenerate with a rational-rules (Goodman et al.) style regeneration proposal: pick a node uniformly and regenerate it from the grammar. 
		 * @param grammar - what grammar to use
		 * @param from - what node are we proposing from
		 * @return A pair of the new proposed tree and the forward-backward log probability (for use in MCMC)
		 */
				
		// copy, regenerate a random node, and return that and forward-backward prob
		#ifdef DEBUG_MCMC
			CERR "REGENERATE" TAB from.string() ENDL;
		#endif

		Node ret = from; // copy

		if(from.sum<double>(can_resample) == 0.0) {
			return std::make_pair(ret, 0.0);
		}
		
		auto s = sample<Node,Node>(ret, can_resample);
		
		double oldgp = grammar->log_probability(*s.first); // reverse probability generating 
		
		s.first->set_to(grammar->generate(s.first->nt())); 
		
		double fb = s.second + grammar->log_probability(*s.first) 
				  - (log(can_resample(*s.first)) - log(ret.sum(can_resample)) + oldgp);

		return std::make_pair(ret, fb);
	}


	std::pair<Node, double> insert_tree(Grammar* grammar, const Node& from) {
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
		
		auto s = sample<Node,Node>(ret, can_resample); // s is a ptr to ret
		Node old_s = *s.first; // the old value of s, copied -- needed for fb
		std::function can_resample_nt = [=](const Node& n) -> double { return can_resample(n)*(n.nt() == s.first->nt()); };
		
		Node t = grammar->generate(s.first->nt()); // make something new of the same type as s
		auto q = sample<Node,Node>(t, can_resample_nt); // q points to something below in t of type s
		
		q.first->set_to(Node(*s.first)); 
	
		// now if we replace something below t with s, there are multiples ones we could have done...
		auto lpq = lp_sample_eq(*s.first, t, can_resample_nt); // 
			
//		CERR "----INSERT-----------" ENDL;
//		CERR from ENDL;
//		CERR *s.first ENDL;
//		CERR t ENDL;
//		CERR s.second TAB grammar->log_probability(t) TAB grammar->log_probability(old_s) TAB lpq ENDL;
		
	
		s.first->set_to(t); // now since s pointed to something in ret, we're done  
		
		// forward is choosing s, generating everything *except* what replaced s, and then replacing
		double forward = s.second +  // must get exactly this s
						 (grammar->log_probability(t)-grammar->log_probability(old_s)) +  // generate the rest of the tree
						 lpq; // probability of getting any s
		
		/// backward is we choose t exactly, then we pick anything below that is equal to s
		double backward = lp_sample_one<Node,Node>(*s.first, ret, can_resample) + 
						  lp_sample_eq<Node,Node>(old_s, *s.first, can_resample_nt);
//		CERR forward TAB backward ENDL;
//		CERR ret ENDL;
		
		return std::make_pair(ret, forward-backward);		
	}
	
		
	std::pair<Node, double> delete_tree(Grammar* grammar, const Node& from) {
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
		
		auto s = sample<Node,Node>(ret, can_resample); // s is a ptr to ret
		Node old_s = *s.first; // the old value of s, copied -- needed for fb

		std::function can_resample_nt = [=](const Node& n) -> double { return can_resample(n)*(n.nt() == s.first->nt()); };
		
		auto q = sample(*s.first, can_resample_nt);
		
		
//		CERR "----DELETE-----------" ENDL;
//		CERR from ENDL;
//		CERR old_s ENDL;		
//		CERR *q.first ENDL;
		
		// forward is choosing s, and then anything equal to q within
		double forward = s.second + lp_sample_eq<Node,Node>(*q.first, *s.first, can_resample_nt); 
		
		// probability of generating everything in s except q
		double tlp = grammar->log_probability(old_s) - grammar->log_probability(*q.first); 
		
		s.first->set_to(Node(*q.first)); // must set with a copy
		
		/// backward is we choose the *new* s, then generate everything else, and choose anything equal
		double backward = lp_sample_one<Node,Node>(*s.first,ret,can_resample) + 
						  tlp + 
						  lp_sample_eq<Node,Node>(*q.first,old_s,can_resample_nt);
							
//		CERR forward TAB backward ENDL;
//		CERR ret ENDL;
		
		return std::make_pair(ret, forward-backward);		
	}
	
}