#pragma once 
 
#include <signal.h>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"
#include "DiscreteDistribution.h"

extern volatile sig_atomic_t CTRL_C;

// This is how we will store the likelihood, indexed by hypothesis, data point, and then 
// giving back a list of prior likelihoods (ordered earliest first) for use with memory decay
typedef std::vector<double> LLvec_t; // how we store the data for each hypothesis
typedef std::map<std::pair<int, int>, LLvec_t> LL_t; // likelihood type

// We store the prediction type as a vector of data_item, hypothesis, map of output to probabilities
template<typename HYP>
using Predict_t = std::vector<std::vector<DiscreteDistribution<typename HYP::output_t>>>; 


/**
 * @class HumanDatum
 * @author piantado
 * @date 03/08/20
 * @file GrammarHypothesis.h
 * @brief This stores the human data, as a list of data to condition on data[0:ndata], the data you predict on, and then correct/total counts
 */
template<typename HYP>
struct HumanDatum {
	HYP::data_t*  data; 
	size_t        ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	HYP::datum_t* predict;	// what we compute on now
	std::map<typename HYP::output_t,size_t> responses; // how many of each type of response do you see?
	double        chance; // how many responses are alltogether possible? Needed for chance responding. 
};


/**
 * @brief Runs MCMC on hypotheses, resampling when the data stops being incremental and returns a unioned
 * 			vector of all the tops
 * @param hypotheses
 * @return 
 */
template<typename HYP>
std::vector<HYP> get_hypotheses_from_mcmc() {
	
	
}

/**
 * @brief 
 * @param hypotheses
 * @return 
 */
template<typename HYP>
Matrix counts(std::vector<HYP>& hypotheses) {
	/* Returns a matrix of hypotheses (rows) by counts of each grammar rule.
	   (requires that each hypothesis use the same grammar) */
	   
	assert(hypotheses.size() > 0);

	size_t nRules = hypotheses[0].grammar->count_rules();

	Matrix out = Matrix::Zero(hypotheses.size(), nRules); 

	#pragma omp parallel for
	for(size_t i=0;i<hypotheses.size();i++) {
		auto c = hypotheses[i].grammar->get_counts(hypotheses[i].get_value());
		Vector cv = Vector::Zero(c.size());
		for(size_t i=0;i<c.size();i++){
			cv(i) = c[i];
		}
		
		out.row(i) = cv;
		assert(hypotheses[i].grammar == hypotheses[0].grammar); // just a check that the grammars are identical
	}
	return out;
}


/**
 * @brief 
 * @param hypotheses
 * @param human_data
 * @return 
 */
template<typename HYP>
LL_t compute_incremental_likelihood(std::vector<HYP>& hypotheses, std::vector<HumanDatum<HYP>>& human_data) {
	// special case of incremental likelihood since we are accumulating data (by sets)
	// So we have written a special case here
	
	// likelihood out will now be a mapping from hypothesis, data_element to prior likelihoods of individual elements
	LL_t out; 
	
	#pragma omp parallel for
	for(size_t h=0;h<hypotheses.size();h++) {
		
		if(!CTRL_C) {
			
			// This will assume that as things are sorted in human_data, they will tend to use
			// the same human_data.data pointer (e.g. they are sorted this way) so that
			// we can re-use prior likelihood items for them
			
			for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
				
				auto idx = std::make_pair(h, di);
				LLvec_t data_lls;
				
				// check if these pointers are equal so we can reuse the previous data			
				if(human_data[di].data != human_data[di-1].data and 
				   human_data[di].ndata > human_data[di-1].ndata) { 
					   
					// just copy over
					data_lls = out[std::make_pair(h, di-1)];
					
					// and fill in the rest
					for(size_t i=human_data[di-1].ndata;i<human_data[di].ndata;i++) {
						data_lls.push_back(  hypotheses[h].compute_single_likelihood( (*human_data[di].data)[i] ));
					}
				}
				else {
					// compute anew; if ndata=0 then we should just include a 0.0
					for(size_t i=0;i<human_data[di].ndata;i++) {
						data_lls.push_back(  hypotheses[h].compute_single_likelihood( (*human_data[di].data)[i] ));
					}				
				}
				
				#pragma omp critical
				out[idx] = data_lls;	
			}
		}
	}
	
	return out;
}

/**
 * @brief 
 * @param hypotheses
 * @param human_data
 * @return 
 */

template<typename HYP, typename data_t>
Predict_t<HYP> model_predictions(std::vector<HYP>& hypotheses, data_t& human_data) {

	Predict_t<HYP> out; 
	
	#pragma omp parallel for
	for(size_t di=0;di<human_data.size();di++) {	
		
		std::vector<DiscreteDistribution<typename HYP::output_t>> v;
		
		for(size_t h=0;h<hypotheses.size();h++) {			
			v.push_back(hypotheses[h](human_data[di].predict->input));
		}
		
		#pragma omp critical
		out.push_back(v);
	}
	return out;	
}

#include "VectorHypothesis.h"

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
	VectorHypothesis params; // logodds forwardalpha and decay
	
	HYP::Grammar_t* grammar;
	Matrix* C;
	LL_t* LL; // type for the likelihood
	Predict_t<HYP>* predictions;
	
	Matrix decayedLikelihood;
	void* which_data_for_likelihood; // store the data we used for decayedLikelihood so we recompute if it changed
	
	GrammarHypothesis() : which_data_for_likelihood(nullptr) {
	}
	
	GrammarHypothesis(HYP::Grammar_t* g, Matrix* c, LL_t* ll, Predict_t<HYP>* p) : grammar(g), C(c), LL(ll), predictions(p), which_data_for_likelihood(nullptr) {
		logA.set_size(grammar->count_rules());
		params.set_size(2); 
	}	
	
	// these unpack our parameters -- NOTE here we multiply by 3 to make it a Normal(0,3) distirbution
	float get_forwardalpha() const { return 1.0/(1.0+exp(-3.0*params(0))); }
	float get_decay() const         { return exp(params(1)/2.0); } // likelihood temperature here has no memory decay
	
	void recompute_decayedLikelihood() {
		double decay = get_decay();
		
		#pragma omp parallel for
		for(int h=0;h<C->rows();h++) {
			for(int di=0;di<decayedLikelihood.cols();di++) {
				
				// sum up with the memory decay
				auto idx = std::make_pair(h,di);
				size_t N = (*LL)[idx].size();
				decayedLikelihood(h,di) = 0.0;
				for(int i=N-1;i>=0;i--) {
					decayedLikelihood(h,di) += (*LL)[idx][i] * pow(N-i, -decay);
				}
				
			}
		}
	}
	
	double compute_prior() override {
		return this->prior = logA.compute_prior() + params.compute_prior();
	}
	
	/**
	 * @brief 
	 * @param data
	 * @param breakout
	 * @return 
	 */		
	virtual double compute_likelihood(const data_t& human_data, const double breakout=-infinity) override {
		// This runs the entire model (computing its posterior) to get the likelihood
		// of the human data
		
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
		
		// and now get the human likelihood, using a binomial likelihood (binomial term not needed)
		auto forwardalpha = get_forwardalpha();
		
		this->likelihood  = 0.0; // of the human data
		
		#pragma omp parallel for
		for(size_t i=0;i<human_data.size();i++) {
			
			// build up a predictive posterior
			DiscreteDistribution<typename HYP::output_t> model_predictions;
			
			for(int h=0;h<hposterior.rows();h++) {
				
				// don't count these in the posterior since they're so small.
				if(hposterior(h,i) < -10) 
					continue;  
				
				for(auto& mp : (*predictions)[i][h].values() ) {
					model_predictions.addmass(mp.first, hposterior(h,i) + mp.second);
				}
			}
			
			double ll = 0.0; // the likelihood here
			auto& di = human_data[i];
			for(auto& r : di.responses) {
				ll += log( (1.0-forwardalpha)*di.chance + forwardalpha*exp(model_predictions[r.first])) * r.second; // log probability times the number of times they answered this
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
		return out;
	}
	
	
	[[nodiscard]] virtual std::pair<self_t,double> propose() const override {
		self_t out = *this;
		
		if(flip(0.1)){
			auto [ h, fb ] = params.propose();
			out.params = h;
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
