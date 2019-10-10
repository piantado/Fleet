#pragma once

#include <deque>
#include <exception>
#include "IO.h"

// an exception for recursing too deep so we can print a trace of what went wrong
class DepthException: public std::exception {} depth_exception;
class EnumerationException: public std::exception {} enumeration_exception;

class Grammar {
	/* 
	 * A grammar stores all of the rules associated with any kind of nonterminal and permits us
	 * to sample as well as compute log probabilities. 
	 * A grammar is considered to own a rule, so it is expected to destroy them in its destructor
	 */

protected:
	std::vector<Rule*> rules[N_NTs];
	double			   Z[N_NTs]; // keep the normalizer handy for each rule
	
public:
	size_t             rule_cumulative_count[N_NTs]; // how many rules are there less than a given nt? (used for indexing)


	Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			Z[i] = 0.0;
			rule_cumulative_count[i] = 0;
		}
	}
	
	Grammar(const Grammar& g) = delete; // should not be doing these
	Grammar(const Grammar&& g) = delete; // should not be doing these
	
	size_t count_nonterminals() const {
		return N_NTs;
	}
	
	
	size_t get_packing_index(const Rule* r) const {
		// A rule's packing index is a unique count index for this rule
		// so that in the whole grammar, we can pack all indices into a vector
		// NOTE: this is not efficiently computed
		bool found = false;
		size_t i=0;
		for(;i<rules[r->nt].size();i++) {
			if(rules[r->nt][i] == r) { found=true; break;}
		}
		assert(found);
		
		return rule_cumulative_count[r->nt]+i; 
	}
	
	size_t count_rules(const nonterminal_t nt) const {
		return rules[nt].size();
	}	
	size_t count_rules() const {
		size_t n=0;
		for(size_t i = 0;i<N_NTs;i++) {
			n += count_rules((nonterminal_t)i);
		}
		return n;
	}
	size_t count_terminals(nonterminal_t nt) const {
		size_t n=0;
		for(auto r : rules[nt]) {
			if(r->N == 0) n++;
		}
		return n;
	}
	size_t count_nonterminals(nonterminal_t nt) const {
		size_t n=0;
		for(auto r : rules[nt]) {
			if(r->N != 0) n++;
		}
		return n;
	}

	
	size_t count_expansions(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return rules[nt].size(); 
	}
	
	Rule* get_rule(const nonterminal_t nt, size_t k) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		assert(k < rules[nt].size());
		return rules[nt][k];
	}
	
	double rule_normalizer(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return Z[nt];
	}
	
	virtual Rule* sample_rule(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		assert(Z[nt] > 0 && "*** It seems there is zero probability of expanding this terminal -- did you include any rules?"); 
		
		double z = rule_normalizer(nt);
		double q = uniform()*z;
		for(auto r: rules[nt]) {
			q -= r->p;
			if(q <= 0.0)
				return r;
		}
		
		assert(0 && "*** Normalizer error in nonterminal "); // Something bad happened in rule probabilities
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const CustomOp o, const int a=0) {
		for(auto r: rules[nt]) {
			if(r->instr.is_a(o) && r->instr.arg == a) 
				return r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const BuiltinOp o, const int a=0) {
		for(auto r: rules[nt]) {
			if(r->instr.is_a(o) && r->instr.arg == a) 
				return r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, size_t i) {
		assert(i <= rules[nt].size());
		return rules[nt][i];
	}
	
	virtual Rule* get_rule(const std::string s) const {
		// returns the rule for which s is a prefix -- but throws errors
		// if there aren't enough
		Rule* ret = nullptr;
		for(size_t nt=0;nt<N_NTs;nt++) {
			for(auto r: rules[nt]) {
				if(is_prefix(s, r->format)){
					if(ret != nullptr) {
						CERR "*** Multiple rules found matching " << s TAB r->format ENDL;
						assert(0);
					}
					ret = r;
				} 
			}
		}
		
		if(ret != nullptr) { // successfully out
			return ret;
		}
		else {			
			CERR "*** No rule found to match " TAB s ENDL;
			assert(0);
		}
	}
	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Computing log probabilities
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	template<typename T> // type needed so we dont' have to import node
	double log_probability(T& n) const {
		
		double lp = 0.0;		
		for(auto& x : n) {
			lp += log(x.rule->p) - log(rule_normalizer(x.rule->nt));
		}
	
		return lp;		
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of converting strings to nodes 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
	template<typename T> 
	T expand_from_names(std::deque<std::string>& q) const {
		// expands an entire stack using nt as the nonterminal -- this is needed to correctly
		// fill in Node::nulldisplay
		assert(!q.empty() && "*** Should not ever get to here with an empty queue -- are you missing arguments?");
		
		std::string pfx = q.front(); q.pop_front();
		assert(pfx != NullRule->format && "NullRule not supported in expand_from_names");

		// otherwise find the matching rule
		Rule* r = this->get_rule(pfx);
		
		T v(r);
		for(size_t i=0;i<r->N;i++) {
			
			if(r->child_types[i] != v.child[i].rule->nt) {
				CERR "*** Grammar expected type " << r->child_types[i] << 
					 " but got type " << v.child[i].rule->nt << " at " << 
					 r->format << " argument " << i ENDL;
				assert(false && "Bad names in expand_from_names."); // just check that we didn't miss this up
			}
			
			v.child[i] = expand_from_names<T>(q);
		}
		return v;
	}
	template<typename T> 
	T expand_from_names(std::string s) const {
		std::deque<std::string> stk = split(s, ':');    
        return expand_from_names<T>(stk);
	}
	
	template<typename T> 
	T expand_from_names(const char* c) const {
		std::string s = c;
        return expand_from_names<T>(s);
	}
	
	
	template<typename T> 
	T expand_from_integer(nonterminal_t nt, size_t z) const {
		// This has some better options than cantor: https://arxiv.org/pdf/1706.04129.pdf
		
		// What we really need is a pairing function where we can assume that
		// the next paired integer is at most k (for whatever k we choose at any point)
		

		size_t numterm = count_terminals(nt);
		if(z < numterm) {
			return T(this->get_rule(nt, z));	// whatever terminal we wanted
		}
		else {
			auto u =  mod_decode(z-numterm, count_nonterminals(nt));
			
			Rule* r = this->get_rule(nt, u.first+numterm); // shift index from terminals (because they are first!)
			T out(r);
			size_t rest = u.second; // the encoding of everything else
			for(size_t i=0;i<r->N;i++) {
				size_t zi; // what is the encoding for the i'th child?
				if(i<r->N-1) { 
					auto ui = rosenberg_strong_decode(rest);
					zi = ui.first;
					rest = ui.second;
				}
				else {
					zi = rest;
				}
				out.child[i] = expand_from_integer<T>(r->child_types[i], zi); // since we are by reference, this should work right
			}
			return out;		
			
		}

	}
			

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of replicating rules 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	
//	virtual double replicating_Z(const nonterminal_t nt) const {
//		// the normalizer for all replicating rules
//		double z = 0.0;
//		for(auto r: rules[nt]) {
//			if(r->replicating_children() > 0) z += r->p;
//		}
//		return z;
//	}
//	
//	
//	virtual Rule* sample_replicating_rule(const nonterminal_t nt) const {
//		
//		double z = replicating_Z(nt);
//		assert(z > 0);
//		double q = uniform()*z;
//		for(auto r: rules[nt]) {
//			if(r->replicating_children() > 0) {
//				q -= r->p;
//				if(q <= 0.0)
//					return r;					
//			}
//		}
//		
//		CERR "*** Normalizer error in nonterminal " << nt;
//		assert(0); // Something bad happened in rule probabilities
//	}

	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Generation
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	

	
	virtual void add(Rule* r) {
		rules[r->nt].push_back(r);
		Z[r->nt] += r->p; // keep track of the total probability
		
		// and keep a count of the cumulative number of rules
		for(size_t nt=r->nt+1;nt<N_NTs;nt++) {
			rule_cumulative_count[nt]++;
		}		
	}

	template<typename T>
	T generate(const nonterminal_t nt, unsigned long depth=0) const {
		// Sample a rule and generate from this grammar. This has a template to avoid a circular dependency
		// and allow us to generate other kinds of things from rules if we want. 
		// We use exceptions here just catch depth exceptions so we can easily get a trace of what
		// happened
		
		if(depth >= Fleet::GRAMMAR_MAX_DEPTH) {
			throw depth_exception; //assert(0);
		}
		
		Rule* r = sample_rule(nt);
		//T n(r, log(r->p) - log(Z[nt])); // slightly inefficient because we compute this twice
		T n = make<T>(r);
		for(size_t i=0;i<r->N;i++) {
			try{
				n.set_child(i, generate<T>(r->child_types[i], depth+1)); // recurse down
			} catch(DepthException& e) {
				CERR "*** Grammar has recursed beyond Fleet::GRAMMAR_MAX_DEPTH (Are the probabilities right?). nt=" << nt << " d=" << depth TAB n.string() ENDL;
				throw e;
			}
		}
		return n;
	}
	
	template<typename T>
	T make(const Rule* r) const {
		return T(r, log(r->p)-log(Z[r->nt]));
	}
	
};
