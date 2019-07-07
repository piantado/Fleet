
/* Here we use the Eigen library in order to represent sets of hypotheses and do the matrix math. 
 * We represent rule counts 
 * */

#include <vector>
#include <tuple>
#include <functional>
#include <Eigen/Dense>


#include "EigenNumerics.h"
#include "GrammarProbabilities.h"


template<typename HYP>
Vector counts(HYP& h) {
	// returns a 1xnRules matrix of how often each rule occurs
	// TODO: This will not work right for Lexicons, where value is not there
	
	Grammar* grammar = h.grammar;
	size_t nRules = h.grammar->count_rules();
	
	Vector out = Vector::Zero(nRules);
		
	h.value->map( [&out, grammar](const Node* n) {
							size_t i = grammar->get_packing_index(n->rule);
							out[i] = out[i]+1;
					});		
	return out;	
}


template<typename HYP>
Matrix counts(std::vector<HYP> hypotheses) {
	/* Returns a matrix of hypotheses (rows) by counts of each grammar rule.
	   (requires that each hypothesis use the same grammar) */
	
	size_t nRules = hypotheses[0].grammar->count_rules();
	
	Matrix out = Matrix::Zero(hypotheses.size(), nRules); 
	
	for(size_t i=0;i<hypotheses.size();i++) {
		out.row(i) = counts(hypotheses[i]);
		assert(hypotheses[i].grammar == hypotheses[0].grammar); // just a check that the grammars are identical
	}
	return out;
}

template<typename HYP>
Matrix incremental_likelihoods(std::vector<HYP> hypotheses, std::vector<typename HYP::t_data> alldata) {
	// Each row here is a hypothesis, and each column is the likelihood for a sequence of data sets
	// Here we check if the previous data point is a subset of the current, and if so, 
	// then we just do the additiona likelihood o fthe set difference 
	// this way, we can pass incremental data without it being too crazy slow
	
	Matrix out = Matrix::Zero(hypotheses.size(), alldata.size()); 
	
	for(size_t h=0;h<hypotheses.size();h++) {
		for(size_t di=0;di<alldata.size();di++) {
			if(di > 0 and alldata[di].size() >= alldata[di-1].size()) { // copy over the previous
				if(std::equal(alldata[di-1].begin(), alldata[di-1].end(), alldata[di].begin())) {
					out(h, di) = out(h,di-1) + hypotheses[h].compute_likelihood(slice(alldata[di], alldata[di-1].size()));
				}
			}
			else {
				out(h,di) =  hypotheses[h].compute_likelihood(alldata[di]);				
			}
		}	
	}
	
	return out;
}

template<typename HYP>
Matrix model_predictions(std::vector<HYP> hypotheses, std::vector<typename HYP::t_datum> predict_data) {
	
	Matrix out = Matrix::Zero(hypotheses.size(), predict_data.size());
	for(size_t h=0;h<hypotheses.size();h++) {
	for(size_t di=0;di<predict_data.size();di++) {
		out(h,di) = 1.0*hypotheses[h].callOne(predict_data[di].input, 0);
	}
	}
	return out;	
}



template<typename HYP>
void simpleGrammarMCMC(std::vector<HYP> hypotheses, 
					   std::vector<typename HYP::t_data> given_data,
					   std::vector<typename HYP::t_datum> predict_data,
					   std::vector<size_t> yes_responses,
					   std::vector<size_t> no_responses
					   ) {
						   
	// NOTE: We have decided NOT to abstract this into anything fancy in order to keep
	// all of the logic of grammar inference in one place and as simple as possible
	
	assert(hypotheses.size() > 0);
	assert(yes_responses.size() == given_data.size()); // one response for each data point
	assert(yes_responses.size() == predict_data.size()); // one response for each data point
	assert(yes_responses.size() == no_responses.size()); // one response for each data point
	
	
	Grammar* grammar = hypotheses[0].grammar;
	
	Matrix C  = counts(hypotheses);
	CERR "# Done computing counts" ENDL;
	
	Matrix LL = incremental_likelihoods(hypotheses, given_data);
	CERR "# Done computing incremental likelihoods " ENDL;
	
	Matrix P  = model_predictions(hypotheses, predict_data); // NOTE the transpose here
	CERR "# Done computing model predictions" ENDL;
	
	
	GrammarHypothesis current(grammar);
	current.randomize(); 
	
	double currentPosterior = -infinity;
	
	// first just print the order
	for(size_t nt=0;nt<N_NTs;nt++) {
		size_t nrules = grammar->count_rules( (nonterminal_t) nt);
		for(size_t i=0;i<nrules;i++) {
			COUT grammar->get_rule((nonterminal_t)nt,i)->format << " ";
		}
	}
	COUT "\n";
	
	for(size_t samples=0;samples<10000000 && !CTRL_C;samples++) {
		auto proposal = current.propose();
		if(currentPosterior == -infinity) proposal.randomize(); // just random until we find something good
		
		Vector hprior = lognormalize(C*proposal.getX());
		
		// Now get the posterior, with the llt scaling factor
		Matrix posterior = (LL*(1.0/proposal.get_llT())).colwise() + hprior; 
		for(size_t i=0;i<posterior.cols();i++) { 	// normalize (must be a faster way) for each amount of given data (column)
			posterior.col(i) = lognormalize(posterior.col(i)).array().exp();
		}
			
		Vector predictive = (posterior.array() * P.array()).colwise().sum(); // elementwise product and then column sum
		
		
		// and now get the human likelihood, using a binomial likelihood (binomial term not needed)
		double proposalLL = 0.0;
		float forwardalpha = proposal.get_forwardalpha();
		float baseline     = proposal.get_baseline();
		for(size_t i=0;i<yes_responses.size();i++) {
			double p = predictive(i)*forwardalpha + (1.0-forwardalpha)*baseline;
			proposalLL += log(p)*yes_responses[i] + log(1.0-p)*no_responses[i];
		}
		
		double proposalPosterior = proposalLL + proposal.prior();
		
		
		// metropolis rule
		if(uniform() < exp(proposalPosterior-currentPosterior) ) {
			current = proposal;
			currentPosterior = proposalPosterior;
		}
		
//		COUT currentPosterior TAB proposalPosterior TAB current.get_llT() TAB current.get_baseline() TAB current.get_forwardalpha() TAB current.getX().transpose() ENDL;

		//COUT proposalPosterior TAB proposal.get_llT() TAB proposal.get_baseline() TAB proposal.get_forwardalpha() TAB proposal.getX().transpose() ENDL;

		COUT currentPosterior  TAB current.get_llT() TAB current.get_baseline() TAB current.get_forwardalpha() TAB current.getX().transpose() ENDL;
	}

	
	
	
}
