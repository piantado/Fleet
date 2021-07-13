#pragma once 

#include "HumanDatum.h"
#include "Control.h"
#include "TopN.h"
#include "MCMCChain.h"
#include "Vectors.h"

/**
 * @brief Runs MCMC on hypotheses, resampling when the data stops being incremental and returns a unioned
 * 			vector of all the tops
 * @param hypotheses
 * @return 
 */
template<typename HYP>
std::set<HYP> get_hypotheses_from_mcmc(const HYP& h0, const std::vector<typename HYP::data_t*>& mcmc_data, Control c, const size_t ntop) {
	
	std::set<HYP> all;	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<mcmc_data.size();vi++) {
		if(CTRL_C) std::terminate();
		
		#pragma omp critical
		{
		COUT "# Running " TAB vi TAB " of " TAB mcmc_data.size() TAB mcmc_data[vi] ENDL;
		}
		
		// start at i=0 so we actually always include prior samples
		// maybe this isn't a good way to do it
		for(size_t i=0;i<=mcmc_data[vi]->size() and !CTRL_C;i++) {
			
			TopN<HYP> top(ntop);
			
			HYP myh0 = h0.restart();
			
			// slices [0,i]
			auto givendata = slice(*(mcmc_data[vi]), 0, i);
			
			MCMCChain chain(myh0, &givendata);
			for(auto& h : chain.run(Control(c))) { // must run on a copy 
				top << h;
			}
			
			#pragma omp critical
			for(auto h : top.values()) {
				h.clear_bayes(); // zero and insert
				all.insert(h);
			}
		}	
	}
	
	return all;
}
