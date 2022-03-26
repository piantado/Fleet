#pragma once 

#include <atomic>

namespace FleetStatistics {
	// Running MCMC/MCTS updates these standard statistics
	
	std::atomic<uintmax_t> posterior_calls(0);	
	std::atomic<uintmax_t> hypothesis_births(0);  // how many total hypotheses have been created? -- useful for tracking when we found a solution
	std::atomic<uintmax_t> vm_ops(0);	
	std::atomic<uintmax_t> mcmc_proposal_calls(0);
	std::atomic<uintmax_t> mcmc_acceptance_count(0);
	std::atomic<uintmax_t> global_sample_count(0);
	std::atomic<uintmax_t> beam_steps(0);
	std::atomic<uintmax_t> enumeration_steps(0);
	
	std::atomic<uintmax_t> depth_exceptions(0); // count up the grammar depth exceptions
	
	
	void reset() {
		posterior_calls = 0;
		hypothesis_births = 0;
		vm_ops = 0;
		mcmc_proposal_calls = 0;
		mcmc_acceptance_count = 0;
		global_sample_count = 0;
		beam_steps = 0;
		enumeration_steps = 0;
	}
}
