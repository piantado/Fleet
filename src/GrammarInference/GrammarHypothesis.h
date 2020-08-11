#pragma once 
 
// TODO: Define these as 2D vectors -- define own class to manage
 
 
#include <signal.h>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"
#include "DiscreteDistribution.h"

#include "Vector2D.h"
#include "HumanDatum.h"
#include "GrammarInferenceUtility.h"

extern volatile sig_atomic_t CTRL_C;

#include "VectorHypothesis.h"

// If we do this, then we compute the grammar hypothesis's predictive values in log space. This is more
// accurate but quite a bit slower, so it's off by default. 
//#define GRAMMAR_HYPOTHESIS_USE_LOG_PREDICTIVE 1

/**
 * @class GrammarHypothesis
 * @author piantado
 * @date 02/08/20
 * @file GrammarHypothesis.h
 * @brief 
 */
template<typename HYP, typename datum_t=HumanDatum<HYP>, typename data_t=std::vector<datum_t>>	// HYP here is the type of the thing we do inference over
class GrammarHypothesis : public MCMCable<GrammarHypothesis<HYP, datum_t, data_t>, datum_t, data_t>  {
public:

	typedef GrammarHypothesis<HYP,datum_t,data_t> self_t;
	
	VectorHypothesis logA; // a simple normal vector for the log of a 
	VectorHypothesis params; // alpha, likelihood temperature, decay
	
	constexpr static int NPARAMS = 3; // how many parameters do we have?
	const static int MAX_DATA_SIZE = 100; // most data we ever see? -- Needed for quickly computing decay factors
	
	HYP::Grammar_t* grammar;
	Matrix* C;
	LL_t* LL; // type for the likelihood
	Predict_t<HYP>* predictions;

	// These variables store some parameters and get recomputed
	// in proposal when necessary 
	Matrix decayedLikelihood;
	double alpha, llt, decay; // the parameters 
	
	void* which_data_for_likelihood;
	
	GrammarHypothesis() : which_data_for_likelihood(nullptr) {	}
	
	GrammarHypothesis(HYP::Grammar_t* g, Matrix* c, LL_t* ll, Predict_t<HYP>* p) : 
		grammar(g), C(c), LL(ll), predictions(p), which_data_for_likelihood(nullptr) {
		logA.set_size(grammar->count_rules());
		params.set_size(NPARAMS); 
	}	
	
	void recompute_alpha() { alpha = 1.0/(1.0+expf(-3.0*params(0))); }
	void recompute_llt()   { llt = expf(params(1)/4.0);	}
	void recompute_decay() { decay = expf(params(2)/2.0); }
	void recompute_decayedLikelihood() {
		// precompute powers because its slow. 
		// we store these in reverse order from some max data size
		// so that we can just take the tail for what we need
		Vector powers = Vector::Zero(MAX_DATA_SIZE);
		for(int i=0;i<MAX_DATA_SIZE;i++) {
			powers[i] = powf(MAX_DATA_SIZE-i,-decay);
		}
		
		#pragma omp parallel for
		// sum up with the memory decay
		for(int h=0;h<C->rows();h++) {
			for(int di=0;di<decayedLikelihood.cols();di++) {
				int l = LL->at(h,di).size(); // how long was that vector?
				assert(l < MAX_DATA_SIZE);
				decayedLikelihood(h,di) = LL->at(h,di).dot(powers(Eigen::lastN(l))) / llt;				
			}
		}
	}
	
	double compute_prior() override {
		return this->prior = logA.compute_prior() + params.compute_prior();
	}
	
	/**
	 * @brief This computes the likelihood of the *human data*.
	 * @param data
	 * @param breakout
	 * @return 
	 */		
	virtual double compute_likelihood(const data_t& human_data, const double breakout=-infinity) override {
		Vector hprior = hypothesis_prior(*C); 
		
		// recompute our likelihood if its the wrong size or not using this data
		if(&human_data != which_data_for_likelihood or 
		   decayedLikelihood.rows() != (int)hprior.size() or 
		   decayedLikelihood.rows() != (int)hprior.size() or 
		   decayedLikelihood.cols() != (int)human_data.size()) {
			which_data_for_likelihood = (void*) &human_data;
			decayedLikelihood = Matrix::Zero(hprior.size(), human_data.size());
			recompute_decayedLikelihood(); // needs the size set above
		}		
		
		Matrix hposterior = decayedLikelihood.colwise() + hprior; // the model's posterior
		
		// now normalize it and convert to probabilities
		#pragma omp parallel for
		for(int i=0;i<hposterior.cols();i++) { 
			hposterior.col(i) = lognormalize(hposterior.col(i)).array();
		}
		
		this->likelihood  = 0.0; // of the human data
		
		#pragma omp parallel for
		for(size_t i=0;i<human_data.size();i++) {
			
			// build up a predictive posterior
			// This is the log version -- if you use this -- you must change the log version down below too. 
			#ifdef GRAMMAR_HYPOTHESIS_USE_LOG_PREDICTIVE
				DiscreteDistribution<typename HYP::output_t> model_predictions;
				for(int h=0;h<hposterior.rows();h++) {
					
					// don't count these in the posterior since they're so small.
					if(hposterior(h,i) < -10) 
						continue;  
					
					for(const auto& mp : predictions->at(h,i).values() ) {
						model_predictions.addmass(mp.first, hposterior(h,i) + mp.second);
					}
				}
			#else
				// build the non-log version (which should be much faster)
				std::map<typename HYP::output_t, double> model_predictions;
				for(int h=0;h<hposterior.rows();h++) {
					for(const auto& mp : predictions->at(h,i).values() ) {
						float p = expf(hposterior(h,i) + mp.second);
						if(model_predictions.find(mp.first) == model_predictions.end()){
							model_predictions[mp.first] = p;
						}
						else {
							model_predictions[mp.first] += p;
						}
					}
				}
			#endif
			
			double ll = 0.0; // the likelihood here
			auto& di = human_data[i];
			for(const auto& r : di.responses) {
				#ifdef GRAMMAR_HYPOTHESIS_USE_LOG_PREDICTIVE
					ll += log( (1.0-alpha)*di.chance + alpha*exp(model_predictions[r.first])) * r.second; // log probability times the number of times they answered this
				#else
					ll += log( (1.0-alpha)*di.chance + alpha*model_predictions[r.first]) * r.second; // log probability times the number of times they answered this
				#endif
			}
			
			#pragma omp critical
			this->likelihood += ll;
		}
		
		return this->likelihood;		
	}
	
	[[nodiscard]] virtual self_t restart() const override {
		self_t out(grammar, C, LL, predictions);
		out.logA = logA.restart();
		out.params = params.restart();	

		out.recompute_alpha();
		out.recompute_llt();
		out.recompute_decay();
		
		return out;
	}
	
	
	[[nodiscard]] virtual std::pair<self_t,double> propose() const override {
		self_t out = *this;
		
		if(flip(0.1)){
			auto [ p, fb ] = params.propose();
			out.params = p;
			
			// figure out what we need to recompute
			if(p(0) != params(0)) out.recompute_alpha();
			if(p(1) != params(1)) out.recompute_llt();
			if(p(2) != params(2)) out.recompute_decay();
			if(p(1) != params(1) or p(2) != params(2))	// note: must come after its computed
				out.recompute_decayedLikelihood();					
			
			return std::make_pair(out, fb);
		}		
		else {
			auto [ h, fb ] = logA.propose();
			out.logA = h;
			return std::make_pair(out, fb);
		}
	}
	
	/**
	 * @brief 
	 * @param C
	 * @return 
	 */	
	Vector hypothesis_prior(Matrix& C) {
		// take a matrix of counts (each row is a hypothesis)
		// and return a prior for each hypothesis under my own X
		// (If this was just a PCFG, which its not, we'd use something like lognormalize(C*proposal.getX()))

		Vector out(C.rows()); // one for each hypothesis
		
		// get the marginal probability of the counts via  dirichlet-multinomial
		Vector allA = logA.value.array().exp(); // translate [-inf,inf] into [0,inf]
		
		#pragma omp parallel for
		for(auto i=0;i<C.rows();i++) {
			
			double lp = 0.0;
			size_t offset = 0; // 
			for(size_t nt=0;nt<grammar->count_nonterminals();nt++) { // each nonterminal in the grammar is a DM
				size_t nrules = grammar->count_rules( (nonterminal_t) nt);
				if(nrules > 1) { // don't need to do anything if only one rule (but must incremnet offset)
					Vector a = eigenslice(allA,offset,nrules); // TODO: seqN doesn't seem to work with this c++ version
					Vector c = eigenslice(C.row(i),offset,nrules);
					double n = a.sum(); assert(n > 0.0); 
					double a0 = c.sum();
					assert(a0 != 0.0);
					lp += std::lgamma(n+1) + std::lgamma(a0) - std::lgamma(n+a0) 
						 + (c.array() + a.array()).array().lgamma().sum() 
						 - (c.array()+1).array().lgamma().sum() 
						 - a.array().lgamma().sum();
				}
				offset += nrules;
			}
		
			out(i) = lp;
		}		
		
		return out;		
	}
	
	virtual bool operator==(const self_t& h) const override {
		return (C == h.C and LL == h.LL and predictions == h.predictions) and 
				(logA == h.logA) and (params == h.params);
	}
	
	
	virtual std::string string() const override {
		// For now we just provide partial information
		return "GrammarHypothesis["+str(this->posterior) + params.string() + ", " + logA.string() + "]";
	}
	
	virtual size_t hash() const override { 
		size_t output = logA.hash();
		hash_combine(output, params.hash());
		return output;
	}
	
};
