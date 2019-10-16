
/* Here we use the Eigen library in order to represent sets of hypotheses and do the matrix math. 
 * We represent rule counts 
 * */

#include <regex>
#include <vector>
#include <tuple>
#include <functional>
#include <Eigen/Dense>
#include <cmath>
#include <unsupported/Eigen/SpecialFunctions>

#include "EigenNumerics.h"
#include "GrammarHypothesis.h"


template<typename HYP>
Vector counts(HYP& h) {
	// returns a 1xnRules matrix of how often each rule occurs
	// TODO: This will not work right for Lexicons, where value is not there
	
	auto c = h.grammar->get_counts(h.value);

	Vector out = Vector::Zero(c.size());
	for(size_t i=0;i<c.size();i++){
		out(i) = c[i];
	}
	
	return out;
}


template<typename HYP>
Matrix counts(std::vector<HYP>& hypotheses) {
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
Matrix incremental_likelihoods(std::vector<HYP>& hypotheses, std::vector<typename HYP::t_data>& alldata) {
	// Each row here is a hypothesis, and each column is the likelihood for a sequence of data sets
	// Here we check if the previous data point is a subset of the current, and if so, 
	// then we just do the additiona likelihood o fthe set difference 
	// this way, we can pass incremental data without it being too crazy slow
	
	Matrix out = Matrix::Zero(hypotheses.size(), alldata.size()); 
	
	for(size_t h=0;h<hypotheses.size();h++) {
		for(size_t di=0;di<alldata.size();di++) {
			if(di > 0 and alldata[di].size() >= alldata[di-1].size() and
			   std::equal(alldata[di-1].begin(), alldata[di-1].end(), alldata[di].begin())) {
					out(h, di) = out(h,di-1) + hypotheses[h].compute_likelihood(slice(alldata[di], alldata[di-1].size()));
			}
			else {
				out(h,di) =  hypotheses[h].compute_likelihood(alldata[di]);				
			}
		}	
	}
	
	return out;
}

template<typename HYP>
Matrix model_predictions(std::vector<HYP>& hypotheses, std::vector<typename HYP::t_datum>& predict_data) {
	
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
	
	// print a header -- ggplot style
	COUT "sample\tposterior\tvariable\tvalue\n";

	
	for(size_t samples=0;samples<10000000 && !CTRL_C; samples++) {
		auto proposal = current.propose();
		if(currentPosterior == -infinity) 
			proposal.randomize(); // just random until we find something good
		
		Vector hprior = proposal.hypothesis_prior(C); 
		
		// Now get the posterior, with the llt scaling factor
		Matrix posterior = LL.colwise() + hprior; 

		// now normalize it and convert to probabilities
		for(int i=0;i<posterior.cols();i++) { 	// normalize (must be a faster way) for each amount of given data (column)
			posterior.col(i) = lognormalize(posterior.col(i)).array().exp();
		}
			
		Vector predictive = (posterior.array() * P.array()).colwise().sum(); // elementwise product and then column sum
		
		
		// and now get the human likelihood, using a binomial likelihood (binomial term not needed)
		float forwardalpha = proposal.get_forwardalpha();
		float baseline     = proposal.get_baseline();
		double proposalLL = 0.0;
		for(size_t i=0;i<yes_responses.size();i++) {
			double p = forwardalpha*predictive(i) + (1.0-forwardalpha)*baseline;
			proposalLL += log(p)*yes_responses[i] + log(1.0-p)*no_responses[i];
		}
//		Vector o = Vector::Ones(predictive.size());
//		Vector p = forwardalpha*predictive(i) + o*(1.0-forwardalpha)*baseline; // vector of probabilities
//		double proposalLL = p.array.log()*yes_responses + (o-p).array.log()*no_responses;
		
		double proposalPosterior = proposalLL + proposal.prior();
		
		// metropolis rule
		if(uniform() < exp(proposalPosterior-currentPosterior) ) {
			current = proposal;
			currentPosterior = proposalPosterior;
		}
		
		// And output, now in ggplot format
		COUT "#" << samples TAB proposalPosterior ENDL;
		COUT samples TAB currentPosterior TAB "baseline" TAB current.get_baseline() ENDL;
		COUT samples TAB currentPosterior TAB "forwardalpha" TAB current.get_forwardalpha() ENDL;
		size_t xi=0;
		for(size_t nt=0;nt<N_NTs;nt++) {
			for(size_t i=0;i<grammar->count_rules( (nonterminal_t) nt);i++) {
				std::string rs = grammar->get_rule((nonterminal_t)nt,i)->format;
				rs = std::regex_replace(rs, std::regex("\\%s"), "X");
				COUT samples TAB currentPosterior TAB QQ(rs) TAB current.getX()(xi) ENDL;
				xi++;
			}
		}
			
				
//		COUT currentPosterior  TAB current.get_llT() TAB current.get_baseline() TAB current.get_forwardalpha() TAB current.getX().transpose() ENDL;
	}

	
	
	
}
