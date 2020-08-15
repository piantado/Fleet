#pragma once 

#include <vector>
#include "DiscreteDistribution.h"
#include "EigenLib.h"

extern volatile sig_atomic_t CTRL_C;



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



