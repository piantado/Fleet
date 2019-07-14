#pragma once 

#include "Numerics.h"
#include "EigenNumerics.h"

class GrammarHypothesis {
	/* This class stores a hypothesis of grammar probabilities */
	
	Vector x; 
	Grammar* grammar;
	float log_llT; // likelihood temperature (stored on a log scale)
	float logodds_baseline; // when we choose at random, whats the probability of true-vs-false?
	float logodds_forwardalpha; 
	
public:
	GrammarHypothesis(Grammar* g, size_t xsize) : grammar(g) {
		x = Vector::Ones(xsize);
		
	}
	GrammarHypothesis(Grammar* g) : grammar(g) {
		x = Vector::Ones(grammar->count_rules());
	}	
//	GrammarProbabilities(const GrammarProbabilities& c) : grammar(c.grammar) {
//		x = c.x;
//		log_llT = c.log_llT;
//		logodds_baseline = c.logodds_baseline;
//		logodds_forwardalpha = c.logodds_forwardalpha;
//	}
//	GrammarHypothesis* copy() const {
//		return new GrammarHypothesis(*this);
//	}
	
	Vector& getX()       { return x; }
	float get_llT()      { return exp(log_llT); }
	float get_baseline() { return exp(-logodds_baseline) /(1+exp(-logodds_baseline)); }
	float get_forwardalpha() { return exp(-logodds_forwardalpha) /(1+exp(-logodds_forwardalpha)); }
	
	double prior() {
		// we're going to put a cauchy prior on the transformed space
		double prior = 0.0;
		for(size_t i=0;i<x.size();i++) {
			prior += cauchy_lpdf(x(i), 0.0, 1.0);
		}
		
		prior += cauchy_lpdf(log_llT, 0.0, 1.0);
		prior += cauchy_lpdf(logodds_baseline, 0.0, 1.0);
		prior += cauchy_lpdf(logodds_forwardalpha, 0.0, 1.0);
		return prior;
	}
	
	void randomize() {
		std::normal_distribution<float> norm(0.0, 1.0);
		log_llT = norm(rng);
		logodds_baseline = norm(rng);		
		logodds_forwardalpha = norm(rng);		
		
		for(size_t nt=0;nt<N_NTs;nt++) {
			size_t nrules= grammar->count_rules( (nonterminal_t) nt);
			auto v = random_multinomial<float>(3.0, nrules); 
			
			// TODO: Should use a fancier built-in 
			for(size_t i=0; i < nrules;i++){ // copy this chunk into x
				x(grammar->rule_cumulative_count[nt]+i) = log(v[i]);
			}
		}
	}
	
	void normalize() {
		// change my own X so it is properly normalized
		
		// and no go through and renormalize
		for(size_t nt=0;nt<N_NTs;nt++) {
			size_t offset = grammar->rule_cumulative_count[nt];
			size_t nrules = grammar->count_rules( (nonterminal_t) nt);
			
			double z = -infinity;
			for(size_t i=0;i<nrules;i++) {
				z = logplusexp<double>(z, x(offset+i));
			}
			for(size_t i=0;i<nrules;i++) {
				x(offset+i) = x(offset+i)-z;
			}
		}
	}	
	
	GrammarHypothesis propose() const {
		GrammarHypothesis out(grammar, x.size());
		
		std::normal_distribution<float> norm(0.0, 1.0);
		//std::exponential_distribution<double> exdist(1.0);
		
		out.log_llT              = log_llT              + 0.1*norm(rng); // likelihood temperature
		out.logodds_baseline     = logodds_baseline     + 0.01*norm(rng);
		out.logodds_forwardalpha = logodds_forwardalpha + 0.01*norm(rng);
		
		// normal proposals in log space
//		for(size_t i=0;i<x.size();i++) {
//			out.getX()(i) = x(i) + 0.10*norm(rng); //*exdist(rng); // randomly scaled
//		}
		
		size_t k = myrandom(x.size());
		for(int i=0;i<x.size();i++) {
			if(i==k) out.getX()(i) = x(i) + 0.1*norm(rng);
			else     out.getX()(i) = x(i);
		}
		
		out.normalize();
		
		return out;	
	}
	
};
