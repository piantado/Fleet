#pragma once 

#include "HumanDatum.h"
#include "Control.h"
#include "TopN.h"
#include "MCMCChain.h"
#include "Vectors.h"

/**
 * @brief Runs MCMC on hypotheses, resampling when the data stops being incremental and returns a unioned
 * 			vector of all the tops. NOTE the wrapper for just singles
 * @param hypotheses
 * @return 
 */
template<typename HYP>
std::vector<std::set<HYP>> get_hypotheses_from_mcmc(const HYP& h0, const std::vector<typename HYP::data_t*>& mcmc_data, Control c, const std::vector<size_t> ntop) {
	assert(not ntop.empty());
	
	// find the largest top
	auto maxntop = *std::max_element(ntop.begin(), ntop.end());
	
	std::vector<std::set<HYP>> out(ntop.size()); // one output for each ntop	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<mcmc_data.size();vi++) {
		if(CTRL_C) std::terminate();
		
		#pragma omp critical
		{
			print("# Running ", vi, " of ", mcmc_data.size(), mcmc_data[vi]);
		}
		
		// start at di=0 so we actually always include prior samples
		// maybe this isn't a good way to do it
		for(size_t di=0;di<=mcmc_data[vi]->size() and !CTRL_C;di++) {
			#pragma omp critical
			{
				COUT "# Running " TAB vi TAB " of " TAB mcmc_data.size() TAB mcmc_data[vi]  TAB di ENDL;
			}
			TopN<HYP> top(maxntop);
			HYP myh0 = h0.restart();
			auto givendata = slice(*(mcmc_data[vi]), 0, di); // slices [0,i]
						
			MCMCChain chain(myh0, &givendata);
			for(auto& h : chain.run(Control(c)) | top | printer(FleetArgs::print) ) { UNUSED(h); } 
			
			#pragma omp critical
			{
				COUT "# Done " TAB vi TAB " of " TAB mcmc_data.size() TAB mcmc_data[vi]  TAB di ENDL;
			}
			
			#pragma omp critical
			{
				// now make data of each size
				for(size_t t=0;t<ntop.size();t++) {
					
					TopN<HYP> mytop(ntop[t]);
					mytop << top; // choose the top ntop
					
					for(auto h : mytop.values()) {
						h.clear_bayes(); // zero and insert
						out[t].insert(h);
					}				
					
				}
			}
		}	
	}
	
	return out;
}

template<typename HYP>
std::set<HYP> get_hypotheses_from_mcmc(const HYP& h0, const std::vector<typename HYP::data_t*>& mcmc_data, Control c, const size_t ntop) {
	auto v = get_hypotheses_from_mcmc(h0, mcmc_data, c, std::vector<size_t>{1,ntop});
	assert(v.size() == 1);
	return *v.begin();
}