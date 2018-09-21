#ifndef MCMC_CPP
#define MCMC_CPP

// A flag to print proposals and posteriors to cerr
//#define DEBUG_MCMC 1

extern volatile sig_atomic_t CTRL_C;

unsigned long global_mcmc_acceptance_count = 0;
unsigned long global_mcmc_proposal_count  = 0;

template<typename HYP>
HYP* MCMC(HYP* current, typename HYP::t_data& data,  void (*callback)(HYP* h), unsigned long steps, unsigned long restart=0, bool returnmax=true) {
    // run MCMC, returning the last sample, and calling callback on each sample. 
    // NOTE: If current has a -inf posterior, we always propose from restart() 
	// NOTE: This requires that HYP implement: propose(), restart(), compute_posterior(const t_data&)
	
	assert(callback != nullptr);
	
	// compute the info for the curent
    current->compute_posterior(data);
    
    if(callback != nullptr)
        callback(current);
		
	HYP* themax = nullptr;
	if(returnmax) 
		themax = new HYP(*current);  		 
    
    unsigned long steps_since_improvement = 0; // how long have I been going without getting better?
    double        best_posterior = current->posterior; // used to measure steps since we've improved
	
    // we'll start at 1 since we did 1 callback on current
    for(unsigned long i=1;i<steps && ! CTRL_C; i++){
        
#ifdef DEBUG_MCMC
        std::cerr << "# Current\t" << current->posterior << "\t" << current->string() << std::endl;
#endif
        
        HYP* proposal;
		if(current->posterior > -infinity) 
			proposal = current->propose();
		else
			proposal = current->restart();
		++global_mcmc_proposal_count;
		
		proposal->compute_posterior(data);

#ifdef DEBUG_MCMC
        std::cerr << "# Proposing\t" << proposal->posterior << "\t" << proposal->string() << std::endl;
#endif
		
		// keep track of the max if we are supposed to
		if(returnmax && (proposal->posterior > themax->posterior)) {
			delete themax; 			
			themax = new HYP(*proposal); 
		}
		
		// keep track of the best
	    if(proposal->posterior > best_posterior) {
			best_posterior = proposal->posterior;
			steps_since_improvement = 0;
		}
		else { // otherwise just keep counting
			steps_since_improvement++;
		}
		
		// use MH acceptance rule
		double ratio = proposal->posterior-current->posterior-proposal->fb; // Remember: don't just check if proposal->posterior>current->posterior or all hell breaks loose
		
		if(   (std::isnan(current->posterior)) ||
		    ((!std::isnan(proposal->posterior)) &&
			  (ratio > 0 || uniform(rng) < exp(ratio)))) {
				  
#ifdef DEBUG_MCMC
			  std::cerr << "# Accept" << std::endl;
#endif
			  delete current; 
			  current = proposal;
			  
			  // count total acceptances
			  ++global_mcmc_acceptance_count;
		}
		else {
			delete proposal; 
		}
			
        // and call on the sample if we meet all our criteria
        callback(current);

		// and finally if we haven't improved then restart
		if(restart>0 && steps_since_improvement > restart){
			auto tmp = current->restart();
			delete current;
			current = tmp;
			current->compute_posterior(data);
			best_posterior = current->posterior; // and reset what it means to be the best
			steps_since_improvement = 0; // reset the couter
			callback(current);
		}
    } // end the main loop

	
	// return either max or current, deleting the other
	if(returnmax){
		delete current;
		return themax;
	}
	else {
		return current;
    }
}



#endif