#ifndef GRAMMAR_H
#define GRAMMAR_H

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
	
	size_t count_expansions(const t_nonterminal nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return rules[nt].size(); 
	}
	
	Rule* get_expansion(const t_nonterminal nt, size_t k) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		assert(k < rules[nt].size());
		return rules[nt][k];
	}
	
	double rule_normalizer(const t_nonterminal nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return Z[nt];
	}
	
	virtual Rule* sample_rule(const t_nonterminal nt) const {
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
		
		CERR "*** Normalizer error in nonterminal " << nt;
		assert(0); // Something bad happened in rule probabilities
	}
		
	virtual void add(Rule* r) {
		rules[r->nonterminal_type].push_back(r);
		Z[r->nonterminal_type] += r->p; // keep track of the total probability
	}

	template<typename T>
	T* generate(const t_nonterminal nt) const {
		// Sample a rule and generate from this grammar. This has a template to avoid a circular dependency
		// and allow us to generate other kinds of things from rules if we want. 
		Rule* r = sample_rule(nt);
		auto n = new T(r, log(r->p) - log(Z[nt])); // slightly inefficient because we compute this twice
		for(size_t i=0;i<r->N;i++) {
			n->child[i] = generate<T>(r->child_types[i]); // recurse down
		}
		return n;
	}
	
	template<typename T>
	T* make(const Rule* r) const {
		return new T(r, log(r->p)-log(Z[r->nonterminal_type]));
	}
	
};

#endif