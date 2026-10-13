#pragma once

#include "TopN.h"
#include "MCMC/MCMCChain.h"
#include "PriorInference.h"



#include <ranges>

template<typename HYP, typename ConditionOn_t=HYP::data_t>
std::vector<HYP> generate_from(std::vector<HumanDatum<HYP,ConditionOn_t>>& human_datas, 
					size_t mcmc_steps_per=1000000, 
					size_t mcmc_top=100,  // this many from each data point
					size_t prior_samples=1000000, 
					size_t prior_top=50000) {
	
	std::set<HYP> hypotheses; 
	
	// here we will defaultly carry over the most recent sample
	// between data sets -- this means that if we are sorted to be
	// incremental, we'll do better
	{
		//auto h0 = HYP::sample();
		
		#pragma omp parallel for
		for(size_t hdi=0;hdi<human_datas.size();hdi++) {
			if(not CTRL_C) {
				auto& hd = human_datas.at(hdi);
				
				TopN<HYP> top(mcmc_top);
				
				Control c(mcmc_steps_per);
					
				// run on each condition_on
				auto h0 = HYP::sample();
				auto chain = MCMCChain(h0, hd.condition_on);
				for(auto& h : chain.run(c) | top ) {
					UNUSED(h);
				}
				
				h0 = top.best(); // start next round on our best from this
				
				// add to this set
				#pragma omp critical
				{
					print("# On", str(hd.condition_on));
					print("# found best: ", h0.prior, h0.likelihood, QQ(h0.string()));

					hypotheses << top; 
				}

			}
		}
		
		print("# MCMC found", hypotheses.size(), "unique samples.");
	}
	
	// and now let's do prior samples
	{
		print("# Running prior samples");
		TopN<HYP> top(prior_top);
		std::vector<typename HYP::datum_t> fakedata;
		PriorInference<HYP> prinf(fakedata);
		for(auto& h : prinf.run(Control(prior_samples)) | top) {
			UNUSED(h);
		}
		hypotheses << top; 
	}
		
	// convert set to vector
	std::vector<HYP> vh(hypotheses.begin(), hypotheses.end());
	return vh;
	
}
