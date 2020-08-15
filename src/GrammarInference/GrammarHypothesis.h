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

#include <array>

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

	// index by hypothesis, data point, an Eigen Vector of all individual data point likelihoods
	typedef Vector2D<std::pair<Vector,Vector>> LL_t; // likelihood type

	// We store the prediction type as a vector of data_item, hypothesis, map of output to probabilities
	typedef Vector2D<DiscreteDistribution<typename HYP::output_t>> Predict_t; 
		
	VectorHypothesis logA; // a simple normal vector for the log of a
	VectorHypothesis params; // alpha, likelihood temperature, decay
	constexpr static int NPARAMS = 3; // how many parameters do we have?
	
	typename HYP::Grammar_t* grammar;
	
	// All of these are shared_ptr so that we can copy hypotheses quickly
	// without recomputing them. NOTE that this assumes that the data does not change
	// between hypotheses of this class. 
	
	std::shared_ptr<Matrix>         C;
	std::shared_ptr<LL_t>           LL; // type for the likelihood
	std::shared_ptr<Predict_t<HYP>> P;

	// These variables store some parameters and get recomputed
	// in proposal when necessary 
	std::shared_ptr<Matrix> decayedLikelihood;
	
	data_t* which_data; // stored so we can remember what we computed for. 
	
	double alpha, llt, decay; // the parameters 
		
	GrammarHypothesis() {	}
	
	GrammarHypothesis(std::vector<HYP>& hypotheses, std::vector<HumanDatum<HYP>>& human_data) {
		set_hypotheses_and_data(hypotheses, human_data);
	}	
	
	void set_hypotheses_and_data(std::vector<HYP>& hypotheses, std::vector<HumanDatum<HYP>>& human_data) {
		
		// set first because it's required below
		which_data = &human_data;
		
		// read the hypothesis from the first grammar, and check its the same for everyone	
		grammar = hypotheses.at(0).grammar;		
		for(auto& h: hypotheses) {
			assert(h.grammar == grammar && "*** It's bad news for GrammarHypothesis if your hypotheses don't all have the same grammar.");
		}
		
		logA.set_size(grammar->count_rules());
		params.set_size(NPARAMS); 
		
		// when we are initialized this way, we compute C, LL, P, and the decayed ll. 
		recompute_C(hypotheses);
		COUT "# Done computing prior counts" ENDL;
		recompute_LL(hypotheses, human_data);
		COUT "# Done computing incremental likelihoods " ENDL;
		recompute_P(hypotheses, human_data);
		COUT "# Done computing model predictions" ENDL;
		recompute_decayedLikelihood(human_data);
	}
	
	void recompute_alpha() { alpha = 1.0/(1.0+expf(-3.0*params(0))); }
	void recompute_llt()   { llt = expf(params(1)/4.0);	} // centered around 1
	void recompute_decay() { decay = expf(params(2)-2); } // peaked near zero
	
	
	/**
	 * @brief Computes our matrix C[h,r] of hypotheses (rows) by counts of each grammar rule.
	 *			(requires that each hypothesis use the same grammar)
	 * @param hypotheses
	 */
	void recompute_C(std::vector<HYP>& hypotheses) {
		   
		assert(hypotheses.size() > 0);

		size_t nRules = hypotheses[0].grammar->count_rules();

		C.reset(new Matrix(hypotheses.size(), nRules)); 

		#pragma omp parallel for
		for(size_t i=0;i<hypotheses.size();i++) {
			auto c = hypotheses[i].grammar->get_counts(hypotheses[i].get_value());
			Vector cv = Vector::Zero(c.size());
			for(size_t i=0;i<c.size();i++){
				cv(i) = c[i];
			}
			
			C->row(i) = cv;
			assert(hypotheses[i].grammar == hypotheses[0].grammar); // just a check that the grammars are identical
		}
	}
		
	/**
	 * @brief Recompute LL[h,di] a hypothesis from each hypothesis and data point to a *vector* of prior responses.
	 * 		  (We need the vector instead of just the sum to implement memory decay
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	void recompute_LL(std::vector<HYP>& hypotheses, std::vector<HumanDatum<HYP>>& human_data) {
		assert(which_data == &human_data);
		
		LL.reset(new LL_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t h=0;h<hypotheses.size();h++) {
			
			if(!CTRL_C) {
				
				// This will assume that as things are sorted in human_data, they will tend to use
				// the same human_data.data pointer (e.g. they are sorted this way) so that
				// we can re-use prior likelihood items for them
				
				for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
					
					Vector data_lls  = Vector::Zero(human_data[di].ndata); // one for each of the data points
					Vector decay_pos = Vector::Zero(human_data[di].ndata); // one for each of the data points
					
					// check if these pointers are equal so we can reuse the previous data			
					if(di > 0 and 
					   human_data[di].data == human_data[di-1].data and 
					   human_data[di].ndata > human_data[di-1].ndata) { 
						   
						// just copy over the beginning
						for(size_t i=0;i<human_data[di-1].ndata;i++){
							data_lls(i)  = LL->at(h,di-1).first(i);
							decay_pos(i) = LL->at(h,di-1).second(i);
						}
						// and fill in the rest
						for(size_t i=human_data[di-1].ndata;i<human_data[di].ndata;i++) {
							data_lls(i)  = hypotheses[h].compute_single_likelihood((*human_data[di].data)[i]);
							decay_pos(i) = human_data[di].decay_position;
						}
					}
					else {
						// compute anew; if ndata=0 then we should just include a 0.0
						for(size_t i=0;i<human_data[di].ndata;i++) {
							data_lls(i) = hypotheses[h].compute_single_likelihood((*human_data[di].data)[i]);
							decay_pos(i) = human_data[di].decay_position;
						}				
					}
			
					// set as an Eigen vector in out
					LL->at(h,di) = std::make_pair(data_lls,decay_pos);
				}
			}
		}
	}
	
	/**
	 * @brief Recomputes the decayed likelihood (e.g. at the given decay level, the total ll for each data point
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	void recompute_decayedLikelihood(const data_t& human_data) {
		assert(which_data == &human_data);
		
		// find the max power we'll ever need
		int MX = -1;
		for(auto& di : human_data) {
			MX = std::max(MX, di.decay_position);
		}
		
		// just compute this once -- should be faster to use vector intrinsics? 
		// we store these in reverse order from some max data size
		// so that we can just take the tail for what we need
		Vector powers = Vector::Zero(MX);
		for(int i=0;i<MX;i++) {
			powers[i] = powf(i,-decay);
		}

		// start with zero
		decayedLikelihood.reset(new Matrix(C->rows(), human_data.size()));
		
		#pragma omp parallel for
		// sum up with the memory decay
		for(int h=0;h<C->rows();h++) {
			for(int di=0;di<decayedLikelihood->cols();di++) {
				const Vector& v = LL->at(h,di).first;
				const Vector  d = powers(LL->at(h,di).second.array());
				decayedLikelihood->operator()(h,di) = v.dot(d) / llt;							
			}
		}
	}
	
	/**
	 * @brief 
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	void recompute_P(std::vector<HYP>& hypotheses, data_t& human_data) {
		assert(which_data == &human_data);
		
		P.reset(new Predict_t<HYP>(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t di=0;di<human_data.size();di++) {	
			for(size_t h=0;h<hypotheses.size();h++) {			
				P->at(h,di) = hypotheses[h](human_data[di].predict->input);
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
		if(&human_data != which_data) { 
			CERR "*** You are computing likelihood on a dataset that the GrammarHypothesis was not constructed with." ENDL
			CERR "		You must call set_hypotheses_and_data before calling this likelihood, but note that when you" ENDL
			CERR "      do that, it will need to recompute everything (which might take a while)." ENDL;
			assert(false);
		}		
		
		Matrix hposterior = decayedLikelihood->colwise() + hprior; // the model's posterior
		
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
					
					for(const auto& mp : P->at(h,i).values() ) {
						model_predictions.addmass(mp.first, hposterior(h,i) + mp.second);
					}
				}
			#else
				// build the non-log version (which should be much faster)
				std::map<typename HYP::output_t, double> model_predictions;
				for(int h=0;h<hposterior.rows();h++) {
					for(const auto& mp : P->at(h,i).values() ) {
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
		self_t out(*this); // copy
		out.logA = logA.restart();
		out.params = params.restart();	

		out.recompute_alpha();
		out.recompute_llt();
		out.recompute_decay();
		
		out.recompute_decayedLikelihood(*out.which_data);
		
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
			if(p(1) != params(1) or p(2) != params(2))	{// note: must come after its computed
				out.recompute_decayedLikelihood(*out.which_data); // must recompute this when we get to likelihood
			}			
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
		return (C == h.C and LL == h.LL and P == h.P) and 
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
