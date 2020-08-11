#pragma once 

#include <vector>
#include "DiscreteDistribution.h"
#include "EigenLib.h"

extern volatile sig_atomic_t CTRL_C;

// index by hypothesis, data point, an Eigen Vector of all individual data point likelihoods
typedef Vector2D<std::pair<Vector,Vector>> LL_t; // likelihood type

// We store the prediction type as a vector of data_item, hypothesis, map of output to probabilities
template<typename HYP>
using Predict_t = Vector2D<DiscreteDistribution<typename HYP::output_t>>; 

#include "Top.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"
#include "Grammar.h"

/**
 * @brief Runs MCMC on hypotheses, resampling when the data stops being incremental and returns a unioned
 * 			vector of all the tops
 * @param hypotheses
 * @return 
 */
template<typename HYP>
std::vector<HYP> get_hypotheses_from_mcmc(HYP& h0, std::vector<typename HYP::data_t*>& mcmc_data, Control c, size_t ntop) {
	
	std::set<HYP> all;	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<mcmc_data.size();vi++) {
		if(!CTRL_C) {  // needed for openmp
		
			#pragma omp critical
			{
			COUT "# Running " TAB vi TAB " of " TAB mcmc_data.size() ENDL;
			}
			
			for(size_t i=0;i<mcmc_data[vi]->size() and !CTRL_C;i++) {
				
				TopN<HYP> top(ntop);
				
				HYP myh0 = h0.restart();
				
				auto givendata = slice(*(mcmc_data[vi]), 0, i);
				
				MCMCChain chain(myh0, &givendata, top);
				chain.run(Control(c)); // must run on a copy 
			
				#pragma omp critical
				for(auto h : top.values()) {
					h.clear_bayes(); // zero and insert
					all.insert(h);
				}
			}
		}
	}
	
	std::vector<HYP> out;
	for(auto& h : all) {
		out.push_back(h);
	}
	return out;
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
	LL_t out(hypotheses.size(), human_data.size()); 
	
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
						data_lls(i)  = out.at(h,di-1).first(i);
						decay_pos(i) = out.at(h,di-1).second(i);
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
				out.at(h,di) = std::make_pair(data_lls,decay_pos);
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
