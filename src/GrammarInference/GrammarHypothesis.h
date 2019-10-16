#pragma once 

#include <Eigen/Core>

#include "Numerics.h"
#include "EigenNumerics.h"

class GrammarHypothesis {
	/* This class stores a hypothesis of grammar probabilities */
	
	Vector x; 
	Grammar* grammar;
	float logodds_baseline; // when we choose at random, whats the probability of true-vs-false?
	float logodds_forwardalpha; 
	
public:
	GrammarHypothesis(Grammar* g) : grammar(g) {
		x = Vector::Ones(grammar->count_rules());
	}	
	
	Vector& getX()                { return x; }
	float get_baseline() const    { return exp(-logodds_baseline) / (1+exp(-logodds_baseline)); }
	float get_forwardalpha()const { return exp(-logodds_forwardalpha) / (1+exp(-logodds_forwardalpha)); }
	
	double prior() {
		// we're going to put a cauchy prior on the transformed space
		double prior = 0.0;
		for(auto i=0;i<x.size();i++) {
			prior += normal_lpdf(x(i), 0.0, 1.0);
		}
		
		prior += normal_lpdf(logodds_baseline, 0.0, 1.0);
		prior += normal_lpdf(logodds_forwardalpha, 0.0, 1.0);
		return prior;
	}
	
	void randomize() {
		logodds_baseline = normal(rng);		
		logodds_forwardalpha = normal(rng);		
		
		for(auto i=0;i<x.size();i++) {
			x(i) = normal(rng);
		}
		
//		size_t offset = 0;
//		for(size_t nt=0;nt<N_NTs;nt++) {
//			size_t nrules= grammar->count_rules( (nonterminal_t) nt);
//			auto v = random_multinomial<float>(3.0, nrules); 
//			
//			// TODO: Should use a fancier built-in 
//			for(size_t i=0; i < nrules;i++){ // copy this chunk into x
//				x(offset+i) = log(v[i]);
//			}
//			offset += nrules;
//		}
	}
	
//	void normalize() {
//		// change my own X so it is properly normalized
//		
//		// and no go through and renormalize
//		size_t offset = 0;
//		for(size_t nt=0;nt<N_NTs;nt++) {
//			size_t nrules = grammar->count_rules( (nonterminal_t) nt);
//			
//			double z = -infinity;
//			for(size_t i=0;i<nrules;i++) {
//				z = logplusexp<double>(z, x(offset+i));
//			}
//			for(size_t i=0;i<nrules;i++) {
//				x(offset+i) = x(offset+i)-z;
//			}
//			
//			offset += nrules;
//		}
//	}	
	
	GrammarHypothesis propose() {
		GrammarHypothesis out = *this;
		
		if(flip()){
			out.logodds_baseline     = logodds_baseline     + 0.1*normal(rng);
			out.logodds_forwardalpha = logodds_forwardalpha + 0.1*normal(rng);
		}		
		else {
			auto i = myrandom(x.size()); // add noise to one coefficient
			out.getX()(i) = x(i) + 0.10*normal(rng);
			
			// Below are all proposals on PCFGs (where x must sum to 1)
		
			// normal proposals in log space
	//		for(size_t i=0;i<x.size();i++) {
	//			out.getX()(i) = x(i) + 0.10*norm(rng); //*exdist(rng); // randomly scaled
	//		}

			// propose to one and renormalize
	//		auto k = myrandom(x.size());
	//		for(auto i=0;i<x.size();i++) {
	//			if(i==k) out.getX()(i) = x(i) + 0.10*norm(rng);
	//			else     out.getX()(i) = x(i);
	//		}
			
			// swap some probability mass
//			out.getX() = getX();
//			auto k1 = myrandom(x.size()), k2 = myrandom(x.size());
//			double zold = logplusexp( out.getX()(k1), out.getX()(k2) );
//			out.getX()(k1) += 0.1*normal(rng); // add random noise
//			double znew = logplusexp( out.getX()(k1), out.getX()(k2) );
//			out.getX()(k1) += (zold-znew); // fix the normalization
//			out.getX()(k2) += (zold-znew); // now, when we logsumexp k1 and k2, we will get znew+(zold-znew)=zold still
	// NOTE: THIS DOES NOT SATISFY DETAILED BALANCE -- STILL CHECK		
		
		
		}
		
		
		
		//out.normalize();
		
		return out;	
	}
	
	Vector hypothesis_prior(Matrix& C) {
		// take a matrix of counts (each row is a hypothesis, is column is a primitive)
		// and return a prior for each hypothesis under my own X
		// (IF this was just  aPCFG, which its not, we'd use something like lognormalize(C*proposal.getX()))

			
		Vector out(C.rows()); // one for each hypothesis
		
		// get the marginal probability of the counts via  dirichlet-multinomial
		Vector etothex = x.array().exp(); // translate [-inf,inf] into [0,inf]
		for(auto i=0;i<C.rows();i++) {
			
			double lp = 0.0;
			size_t offset = 0; // 
			for(size_t nt=0;nt<N_NTs;nt++) { // each nonterminal in the grammar is a DM
				size_t nrules = grammar->count_rules( (nonterminal_t) nt);
				if(nrules==1) continue;
				Vector x = eigenslice(etothex,offset,nrules); // TODO: seqN doesn't seem tow ork with this c++ version
				Vector a = eigenslice(C.row(i),offset,nrules);
				double n = a.sum();
				double a0 = x.sum();
				lp += std::lgamma(n+1) + std::lgamma(a0) - std::lgamma(n+a0) 
				     + (x.array() + a.array()).array().lgamma().sum() 
					 - (x.array()+1).array().lgamma().sum() 
					 - a.array().lgamma().sum();
				CERR nt TAB n TAB a0 TAB lp ENDL;
				offset += nrules;
			}
		
			out(i) = lp;
			CERR out(i) TAB lp ENDL;
		}
		
		
		return out;		
	}
	
	
};
