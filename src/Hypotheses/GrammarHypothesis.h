#pragma once 
 
#include <signal.h>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"

extern volatile sig_atomic_t CTRL_C;

// This is how we will store the likelihood, indexed by hypothesis, data point, and then 
// giving back a list of prior likelihoods (ordered earliest first) for use with memory decay
typedef std::map<std::pair<int, int>, std::vector<double>> LL_t; // likelihood type

/**
 * @class HumanDatum
 * @author piantado
 * @date 02/08/20
 * @file GrammarHypothesis.h
 * @brief 
 */

//template<typename t_learnerdatum, typename t_learnerdata=std::vector<t_learnerdatum>>
//struct HumanDatum { 
//	// a data structure to store our loaded human data. This stores responses for a prediction
//	// conditioned on given data, with yes/no responses
//	// We have T as our own inherited type so we can write comparisons, is_data_prefix etc for subtypes
//	// NOTE: we may want to abstract out a response type as well
//	
//	size_t cntyes; // yes and no responses
//	size_t cntno;
//	t_learnerdata  given_data;   // what data they saw
//	t_learnerdatum predictdata; // what they responded to
//};



template<typename HYP>
struct HumanDatum {
	HYP::data_t*  data; 
	HYP::datum_t* predict_data;	// what we compute on now
	size_t        ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	size_t        count_correct;
	size_t        total_count; 
};


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
template<typename HYP, typename data_t>
Matrix incremental_likelihoods(std::vector<HYP>& hypotheses, data_t& human_data) {
	// Each row here is a hypothesis, and each column is the likelihood for a sequence of data sets
	// TODO: In general this can be sped up by checking if we've already computed part of the likelihood
	//       earlier -- thats' what GemmarInfernce/Main.cpp::my_compute_incremental_likelihood does
	
	Matrix out = Matrix::Zero(hypotheses.size(), human_data.size()); 
	#pragma omp parallel for
	for(size_t h=0;h<hypotheses.size();h++) {
		if(!CTRL_C) {
			for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
				out(h,di) =  hypotheses[h].compute_likelihood(human_data[di].given_data);			
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
Matrix model_predictions(std::vector<HYP>& hypotheses, data_t& human_data) {
	
	Matrix out = Matrix::Zero(hypotheses.size(), human_data.size());
	#pragma omp parallel for
	for(size_t h=0;h<hypotheses.size();h++) {
	for(size_t di=0;di<human_data.size();di++) {
		out(h,di) =  1.0*hypotheses[h].callOne(human_data[di].predictdata.x, 0);
	}
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
template<typename Grammar_t, typename datum_t, typename data_t=std::vector<datum_t>>	// HYP here is the type of the thing we do inference over
class GrammarHypothesis : public MCMCable<GrammarHypothesis<Grammar_t, datum_t, data_t>, datum_t, data_t>  {
	/* This class stores a hypothesis of grammar probabilities. The data_t here is defined to be the above tuple and datum is ignored */
	// TODO: Change to use a generic vector of parameters that we can convert to whatever we want -- temperature, memory decay, etc. 

public:

	typedef GrammarHypothesis<Grammar_t,datum_t,data_t> self_t;
	
	VectorHypothesis logA; // a simple normal vector for the log of a 
	VectorHypothesis params; // logodds baseline and forwardalpha
	
	Grammar_t* grammar;
	Matrix* C;
	LL_t* LL; // type for the likelihood
	Matrix* P;
	
	Matrix likelihoodHere;
	void* which_data_for_likelihood; // store the data we used for likelihoodHere so we recompute if it changed
	
	GrammarHypothesis() : which_data_for_likelihood(nullptr) {
	}
	
	GrammarHypothesis(Grammar_t* g, Matrix* c, LL_t* ll, Matrix* p) : grammar(g), C(c), LL(ll), P(p), which_data_for_likelihood(nullptr) {
		logA.set_size(grammar->count_rules());
		params.set_size(3); 
	}	
	
	// these unpack our parameters -- NOTE here we multiply by 3 to make it a Normal(0,3) distirbution
	float get_baseline() const    { return 1.0/(1.0+exp(-3.0*params(0))); }
	float get_forwardalpha()const { return 1.0/(1.0+exp(-3.0*params(1))); }
	float get_decay() const         { return exp(params(2)/2.0); } // likelihood temperature here has no memory decay
	
	void recompute_likelihoodHere() {
		double decay = get_decay();
		
		#pragma omp parallel for
		for(int h=0;h<C->rows();h++) {
			for(int di=0;di<likelihoodHere.cols();di++) {
				
				// sum up with the memory decay
				auto idx = std::make_pair(h,di);
				size_t N = (*LL)[idx].size();
				likelihoodHere(h,di) = 0.0;
				for(int i=N-1;i>=0;i--) {
					likelihoodHere(h,di) += (*LL)[idx][i] * pow(N-i, -decay);
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
		   likelihoodHere.rows() != (int)hprior.size() or 
		   likelihoodHere.rows() != (int)hprior.size() or 
		   likelihoodHere.cols() != (int)human_data.size()) {
			which_data_for_likelihood = (void*) &human_data;
			likelihoodHere = Matrix::Zero(hprior.size(), human_data.size());
			recompute_likelihoodHere(); // needs the size set above
		}
		
		
		Matrix hposterior = likelihoodHere.colwise() + hprior; // the model's posterior

		// now normalize it and convert to probabilities
		#pragma omp parallel for
		for(int i=0;i<hposterior.cols();i++) { 	// normalize (must be a faster way) for each amount of given data (column)
			hposterior.col(i) = lognormalize(hposterior.col(i)).array().exp();
		}
			
		Vector hpredictive = (hposterior.array() * (*P).array()).colwise().sum(); // elementwise product and then column sum
		
		// and now get the human likelihood, using a binomial likelihood (binomial term not needed)
		auto forwardalpha = get_forwardalpha();
		auto baseline     = get_baseline();
		this->likelihood = 0.0; // of the human data
		for(size_t i=0;i<human_data.size();i++) {
			double p = forwardalpha*hpredictive(i) + (1.0-forwardalpha)*baseline;
			this->likelihood += log(p)*human_data[i].cntyes + log(1.0-p)*human_data[i].cntno;
		}
		
		return this->likelihood;		
	}
	
	[[nodiscard]] virtual self_t restart() const override {
		self_t out(grammar, C, LL, P);
		out.logA = logA.restart();
		out.params = params.restart();		
		return out;
	}
	
	
	[[nodiscard]] virtual std::pair<self_t,double> propose() const override {
		self_t out = *this;
		
		if(flip(0.1)){
			auto [ h, fb ] = params.propose();
			out.params = h;
			out.recompute_likelihoodHere();
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
