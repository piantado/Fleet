#pragma once

#include <deque>
#include <exception>

// an exception for recursing too deep so we can print a trace of what went wrong
class DepthException: public std::exception {} depth_exception;

class Grammar {
	/* 
	 * A grammar stores all of the rules associated with any kind of nonterminal and permits us
	 * to sample as well as compute log probabilities. 
	 * A grammar is considered to own a rule, so it is expected to destroy them in its destructor
	 */

protected:
	std::vector<Rule*> rules[N_NTs];
	double			   Z[N_NTs]; // keep the normalizer handy for each rule
	const unsigned long MAX_DEPTH = 64; 
	
public:
	Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			Z[i] = 0.0;
		}
	}
	
	virtual ~Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			for(auto rp : rules[i]) {
				delete rp; // delete this rule
			}
		}
	}
	
	size_t count_nonterminals() const {
		return N_NTs;
	}
	
	size_t count_expansions(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return rules[nt].size(); 
	}
	
	Rule* get_expansion(const nonterminal_t nt, size_t k) const {
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
		assert(Z[nt] > 0); 
		
		double z = Z[nt];
		double q = uniform(rng)*z;
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
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Computing log probabilities
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	template<typename T> // type needed so we dont' have to import node
	double log_probability(const T* n) const {
		assert(n != nullptr);
		
		double lp = log(n->rule->p) - log(rule_normalizer(n->rule->nt));
		
		for(size_t i=0;i<n->rule->N;i++) {
			if(n->child[i] != nullptr)
				lp += log_probability<T>(n->child[i]);
		}
		return lp;		
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of converting strings to nodes 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	Rule* get_from_string(const std::string s) const {
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
	
	template<typename T> 
	T* expand_from_names(std::deque<std::string>& q) const {
		// expands an entire stack using nt as the nonterminal -- this is needed to correctly
		// fill in Node::nulldisplay
		assert(!q.empty() && "*** Should not ever get to here with an empty queue -- are you missing arguments?");
		
		std::string pfx = q.front(); q.pop_front();
		if(pfx == T::nulldisplay) return nullptr; // for gaps!

		// otherwise find the matching rule
		Rule* r = this->get_from_string(pfx);
		
		T* v = make<T>(r);
		for(size_t i=0;i<r->N;i++) {
			v->child[i] = expand_from_names<T>(q);
			if(v->child[i] != nullptr && r->child_types[i] != v->child[i]->rule->nt) {
				CERR "*** Grammar expected type " << r->child_types[i] << 
					 " but got type " << v->child[i]->rule->nt << " at " << 
					 r->format << " argument " << i ENDL;
				assert(false && "Bad names in expand_from_names."); // just check that we didn't miss this up
			}
		}
		return v;
	}
	
	template<typename T> 
	T* expand_from_names(std::string s) const {
		std::deque<std::string> stk = split(s, ':');    
        return expand_from_names<T>(stk);
	}
	
	template<typename T> 
	T* expand_from_names(const char* c) const {
		std::string s = c;
        return expand_from_names<T>(s);
	}
		

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of replicating rules 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	virtual double replicating_Z(const nonterminal_t nt) const {
		// the normalizer for all replicating rules
		double z = 0.0;
		for(auto r: rules[nt]) {
			if(r->replicating_children() > 0) z += r->p;
		}
		return z;
	}
	
	
	virtual Rule* sample_replicating_rule(const nonterminal_t nt) const {
		
		double z = replicating_Z(nt);
		assert(z > 0);
		double q = uniform(rng)*z;
		for(auto r: rules[nt]) {
			if(r->replicating_children() > 0) {
				q -= r->p;
				if(q <= 0.0)
					return r;					
			}
		}
		
		CERR "*** Normalizer error in nonterminal " << nt;
		assert(0); // Something bad happened in rule probabilities
	}

	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Generation
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	

	
	virtual void add(Rule* r) {
		rules[r->nt].push_back(r);
		Z[r->nt] += r->p; // keep track of the total probability
	}

	template<typename T>
	T* generate(const nonterminal_t nt, unsigned long depth=0) const {
		// Sample a rule and generate from this grammar. This has a template to avoid a circular dependency
		// and allow us to generate other kinds of things from rules if we want. 
		// We use exceptions here just catch depth exceptions so we can easily get a trace of what
		// happened
		
		if(depth >= MAX_DEPTH) {
			throw depth_exception; //assert(0);
		}
		
		Rule* r = sample_rule(nt);
		auto n = new T(r, log(r->p) - log(Z[nt])); // slightly inefficient because we compute this twice
		for(size_t i=0;i<r->N;i++) {
			try{
				n->child[i] = generate<T>(r->child_types[i], depth+1); // recurse down
			} catch(DepthException& e) {
				CERR "*** Grammar has recursed beyond MAX_DEPTH (Are the probabilities right?). nt=" << nt << " d=" << depth TAB n->string() ENDL;
				throw e;
			}
		}
		return n;
	}
	
	template<typename T>
	T* make(const Rule* r) const {
		return new T(r, log(r->p)-log(Z[r->nt]));
	}
	
};

