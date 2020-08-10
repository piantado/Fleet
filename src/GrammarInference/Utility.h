#pragma once 

#include <vector>
#include "DiscreteDistribution.h"
#include "EigenLib.h"

extern volatile sig_atomic_t CTRL_C;

// This is how we will store the likelihood, indexed by hypothesis, data point, and then 
// giving back a list of prior likelihoods (ordered earliest first) for use with memory decay
typedef std::vector<double> LLvec_t; // how we store the data for each hypothesis
typedef std::map<std::pair<int, int>, LLvec_t> LL_t; // likelihood type

// We store the prediction type as a vector of data_item, hypothesis, map of output to probabilities
template<typename HYP>
using Predict_t = Vector2D<DiscreteDistribution<typename HYP::output_t>>; 


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
				if(di > 0 and 
				   human_data[di].data == human_data[di-1].data and 
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

	Predict_t<HYP> out(hypotheses.size(), human_data.size()); 
	
	#pragma omp parallel for
	for(size_t di=0;di<human_data.size();di++) {	
		for(size_t h=0;h<hypotheses.size();h++) {			
			out.at(h,di) = hypotheses[h](human_data[di].predict->input);
		}
	}
	return out;	
}
