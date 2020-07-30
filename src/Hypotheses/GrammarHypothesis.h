#pragma once 
 
#include <signal.h>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"

extern volatile sig_atomic_t CTRL_C;

template<typename t_learnerdatum, typename t_learnerdata=std::vector<t_learnerdatum>>
struct HumanDatum { 
	// a data structure to store our loaded human data. This stores responses for a prediction
	// conditioned on given data, with yes/no responses
	// We have T as our own inherited type so we can write comparisons, is_data_prefix etc for subtypes
	// NOTE: we may want to abstract out a response type as well
	
	size_t cntyes; // yes and no responses
	size_t cntno;
	t_learnerdata  given_data;   // what data they saw
	t_learnerdatum predictdata; // what they responded to
};


/* Helper functions for computing counts, C, LL, P */

template<typename HYP>
Vector counts(HYP& h) {
	// returns a 1xnRules matrix of how often each rule occurs
	// TODO: This will not work right for Lexicons, where value is not there
	
	auto c = h.grammar->get_counts(h.get_value());

	Vector out = Vector::Zero(c.size());
	for(lsize_t i=0;i<c.size();i++){
		out(i) = c[i];
	}
	
	return out;
}


template<typename HYP>
Matrix counts(std::vector<HYP>& hypotheses) {
	/* Returns a matrix of hypotheses (rows) by counts of each grammar rule.
	   (requires that each hypothesis use the same grammar) */
	   
	assert(hypotheses.size() > 0);

	size_t nRules = hypotheses[0].grammar->count_rules();

	Matrix out = Matrix::Zero(hypotheses.size(), nRules); 

	for(size_t i=0;i<hypotheses.size();i++) {
		out.row(i) = counts(hypotheses[i]);
		assert(hypotheses[i].grammar == hypotheses[0].grammar); // just a check that the grammars are identical
	}
	return out;
}


template<typename HYP, typename data_t>
Matrix incremental_likelihoods(std::vector<HYP>& hypotheses, data_t& human_data) {
	// Each row here is a hypothesis, and each column is the likelihood for a sequence of data sets
	// Here we check if the previous data point is a subset of the current, and if so, 
	// then we just do the additiona likelihood o fthe set difference 
	// this way, we can pass incremental data without it being too crazy slow
	
	Matrix out = Matrix::Zero(hypotheses.size(), human_data.size()); 
	
	for(size_t h=0;h<hypotheses.size() and !CTRL_C;h++) {
		for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
			out(h,di) =  hypotheses[h].compute_likelihood(human_data[di].given_data);			
		}	
	}
	
	return out;
}

template<typename HYP, typename data_t>
Matrix model_predictions(std::vector<HYP>& hypotheses, data_t& human_data) {
	
	Matrix out = Matrix::Zero(hypotheses.size(), human_data.size());
	for(size_t h=0;h<hypotheses.size();h++) {
	for(size_t di=0;di<human_data.size();di++) {
		out(h,di) =  1.0*hypotheses[h].callOne(human_data[di].predictdata.x, 0);
	}
	}
	return out;	
}

#include "VectorHypothesis.h"

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
	Matrix* LL;
	Matrix* P;
	
	GrammarHypothesis() {
	}
	
	GrammarHypothesis(Grammar_t* g, Matrix* c, Matrix* ll, Matrix* p) : grammar(g), C(c), LL(ll), P(p) {
		logA.set_size(grammar->count_rules());
		params.set_size(2); 
	}	
	
	// these unpack our parameters
	float get_baseline() const    { return 1.0/(1.0+exp(-params(0))); }
	float get_forwardalpha()const { return 1.0/(1.0+exp(-params(1))); }
	
	double compute_prior() override {
		return this->prior = logA.compute_prior() + params.compute_prior();
	}
	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// This runs the entire model (computing its posterior) to get the likelihood
		// of the human data
		
		Vector hprior = hypothesis_prior(*C); 
		Matrix hposterior = (*LL).colwise() + hprior; // the model's posterior

		// now normalize it and convert to probabilities
		for(int i=0;i<hposterior.cols();i++) { 	// normalize (must be a faster way) for each amount of given data (column)
			hposterior.col(i) = lognormalize(hposterior.col(i)).array().exp();
		}
			
		Vector hpredictive = (hposterior.array() * (*P).array()).colwise().sum(); // elementwise product and then column sum
		
		// and now get the human likelihood, using a binomial likelihood (binomial term not needed)
		auto forwardalpha = get_forwardalpha();
		auto baseline     = get_baseline();
		this->likelihood = 0.0; // of the human data
		for(size_t i=0;i<data.size();i++) {
			double p = forwardalpha*hpredictive(i) + (1.0-forwardalpha)*baseline;
			this->likelihood += log(p)*data[i].cntyes + log(1.0-p)*data[i].cntno;
		}
//		Vector o = Vector::Ones(predictive.size());
//		Vector p = forwardalpha*predictive(i) + o*(1.0-forwardalpha)*baseline; // vector of probabilities
//		double proposalLL = p.array.log()*yes_responses + (o-p).array.log()*no_responses;
		
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
		
		if(flip()){
			auto [ h, fb ] = params.propose();
			out.params = h;
			return std::make_pair(out, fb);
		}		
		else {
			auto [ h, fb ] = logA.propose();
			out.params = h;
			return std::make_pair(out, fb);
		}
	}
	
	Vector hypothesis_prior(Matrix& C) {
		// take a matrix of counts (each row is a hypothesis, is column is a primitive)
		// and return a prior for each hypothesis under my own X
		// (IF this was just  aPCFG, which its not, we'd use something like lognormalize(C*proposal.getX()))

		Vector out(C.rows()); // one for each hypothesis
		
		// get the marginal probability of the counts via  dirichlet-multinomial
		Vector allA = logA.value.array().exp(); // translate [-inf,inf] into [0,inf]
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
