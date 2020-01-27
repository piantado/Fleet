#pragma once

/* NOTE: The insert/delete detailed balance have not been checked done yet */

#include <utility>
#include <tuple>

namespace Proposals { 
		
	double can_resample(const Node& n) {
		return n.can_resample*1.0;
	}

	std::pair<Node,double> regenerate(Grammar* grammar, const Node& from) {
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
		
		if(s.first->parent == nullptr) {
			*s.first = grammar->generate(s.first->rule->nt); // make something new of the same type
		}
		else {
			s.first->parent->set_child(s.first->pi, grammar->generate(s.first->rule->nt)); 
		}
		
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
		std::function can_resample_nt = [=](const Node& n) -> double { return can_resample(n)*(n.nt() == s.first->nt()); };
		
		double slp = grammar->log_probability(*s.first);
		Node t = grammar->generate(s.first->nt()); // make something new of the same type 
		auto q = sample<Node,Node>(t, can_resample_nt); // q points to something below in t of type s
		
		q.first->set_to(Node(*s.first)); 
		s.first->set_to(t); // now since s pointed to something in ret, we're done  
		
		// forward is choosing s, generating everything *except* what replaced s, and then replacing
		double forward = s.second + (grammar->log_probability(t)-slp) + q.second; 
		
		/// backward is we choose t, then we pick anything below that is equal to s
		double backward = p_sample_eq<Node,Node>(t,ret,can_resample) + 
						  p_sample_eq<Node,Node>(*s.first, t, can_resample_nt);
//		CERR "INSERT" TAB from.string() TAB ret.string() ENDL;
		
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
		std::function can_resample_nt = [=](const Node& n) -> double { return can_resample(n)*(n.nt() == s.first->nt()); };
		
		auto q = sample(*s.first, can_resample_nt);
		
		// forward is choosing s, generating everything *except* what replaced s, and then replacing
		double forward = s.second + p_sample_eq<Node,Node>(*q.first, *s.first, can_resample_nt); 
		
		// probability of generating everything in s except q
		double tlp = grammar->log_probability(*s.first) - grammar->log_probability(*q.first); 
		
		s.first->set_to(Node(*q.first)); // must set with a copy
		
		/// backward is we choose the *new* s, then generate everything else, and choose 
		double backward = p_sample_eq<Node,Node>(*s.first,ret,can_resample) + tlp;
		
		
		// NOTE: TODO: The above is not quite right because we could have chosen a bunch of different ses
		// I think we need to use an aux variable argument on choosing s
//		CERR "DELETE" TAB from.string() TAB ret.string() ENDL;
		
		return std::make_pair(ret, forward-backward);		
	}
	
}