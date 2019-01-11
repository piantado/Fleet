#ifndef MCMC_CPP
#define MCMC_CPP

// A flag to print proposals and posteriors to cerr
//#define DEBUG_MCMC 1

extern volatile sig_atomic_t CTRL_C;

template<typename HYP>
struct parallel_MCMC_args { 
	HYP* current;
	typename HYP::t_data* data;
	void (*callback)(HYP*);
	unsigned long steps; 
	unsigned long restart;
	bool returnmax;
	unsigned long time;
};


template<typename HYP>
void* parallel_MCMC_helper( void* args) {
	auto a = (parallel_MCMC_args<HYP>*)args;
	
	a->current = MCMC(a->current, *a->data, a->callback, a->steps, a->restart, a->returnmax, a->time); // doesn't matter if we returnmax
	pthread_exit(nullptr);
}


template<typename HYP>
HYP* parallel_MCMC(size_t cores, HYP* current, typename HYP::t_data* data,  
		void (*callback)(HYP* h), unsigned long steps, unsigned long restart=0, bool returnmax=true, unsigned long time=0) {
	// here we have no returnmax, always returns void
	
	if(cores == 1)  { // don't use parallel, for easier debugging
		return MCMC(current, *data, callback, steps, restart, returnmax, time);
	}
	else {
	
		pthread_t threads[cores]; 
		struct parallel_MCMC_args<HYP> args[cores];
		
		for(unsigned long t=0;t<cores;t++) {
			args[t].current = (t==cores-1 ? current : current->restart()); // have to make the *last* one current so we can use it to set up the others
			args[t].data = data;
			args[t].callback = callback;
			args[t].steps=steps;
			args[t].restart=restart;
			args[t].returnmax = returnmax;
			args[t].time = time;
	
			// run
			int rc = pthread_create(&threads[t], nullptr, parallel_MCMC_helper<HYP>, &args[t]);
			if(rc) assert(0 && "Failed to create thread");
		}
		
		// wait for all to complete
		for(unsigned long t=0;t<cores;t++) {
			pthread_join(threads[t], nullptr);     
		}
		
		// find the max to return
		size_t maxi = 0;
		if(returnmax) { // find the max to retunr if we should, otherwise return maxi=0
			for(size_t i=1;i<cores;i++){
				if(args[i].current->posterior > args[maxi].current->posterior) {
					maxi = i;
				}
			}
		}
		
		// clean up everything except what you return
		for(size_t i=0;i<cores;i++){
			if(i != maxi) // 
				delete args[i].current;
		}
		
		return args[maxi].current; 		
	}
}


template<typename HYP>
HYP* MCMC(HYP* current, typename HYP::t_data& data,  void (*callback)(HYP* h), unsigned long steps, 
		  unsigned long restart=0, bool returnmax=true, unsigned long time=0) {
    // run MCMC, returning the last sample, and calling callback on each sample. 
	// this either runs steps worth of time, or time worth of *seconds*
	// if either of these is 0, then that is ignored; if both are nonzero, then it uses whatever is 
    // NOTE: If current has a -inf posterior, we always propose from restart() 
	// NOTE: This requires that HYP implement: propose(), restart(), compute_posterior(const t_data&)
	const double temperature = 1.0;
	
	using clock = std::chrono::steady_clock;
	
	assert(callback != nullptr); // better not run with null callback
	
	// compute the info for the curent
    current->compute_posterior(data);
	callback(current);
	++FleetStatistics::global_sample_count;

	// copy the current to keep track of the max, if that's what we're supposed to return
	HYP* themax = nullptr;
	if(returnmax) themax = current->copy();
	
    unsigned long steps_since_improvement = 0; // how long have I been going without getting better?
    double        best_posterior = current->posterior; // used to measure steps since we've improved
	auto          start_time = clock::now();
		
    // we'll start at 1 since we did 1 callback on current to begin
    for(unsigned long i=1; (i<steps || steps==0) && !CTRL_C; i++){
        
		// check the elapsed time 
		if(time > 0) {
			double elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start_time).count();
			if(elapsed_time > time) break;
		}
		
		#ifdef DEBUG_MCMC
			std::cerr << "\n# Current\t" << current->posterior TAB current->prior TAB current->likelihood TAB "\t" TAB current->string() ENDL;
		#endif
        
		// generate the proposal -- defaulty "restarting" if we're currently at -inf
		HYP* proposal;
		double fb = 0.0;
		if(current->posterior > -infinity) {
			std::tie(proposal,fb) = current->propose();
		}
		else {
			proposal = current->restart();
		}
			
		++FleetStatistics::mcmc_proposal_calls;
		
		// actually compute the posterior on the dat 
		proposal->compute_posterior(data);

		#ifdef DEBUG_MCMC
			std::cerr << "# Proposed \t" << proposal->posterior TAB proposal->prior TAB proposal->likelihood TAB fb TAB proposal->string() ENDL;
		#endif
		
		// keep track of the max if we are supposed to
		if(returnmax && (proposal->posterior > themax->posterior)) {
			delete themax; 			
			themax = proposal->copy(); 
		}
		
		// keep track of the best on this run -- note we can't compute from themax because 
		// that needs to be shared across runs
	    if(proposal->posterior > best_posterior) {
			best_posterior = proposal->posterior;
			steps_since_improvement = 0;
		}
		else { // otherwise just keep counting
			steps_since_improvement++;
		}
		
		// use MH acceptance rule, with some fanciness for NaNs
		double ratio = proposal->posterior/temperature - current->posterior/temperature - fb; // Remember: don't just check if proposal->posterior>current->posterior or all hell breaks loose		
		if(   (std::isnan(current->posterior)) ||
		    ((!std::isnan(proposal->posterior)) &&
			  (ratio > 0 || uniform(rng) < exp(ratio)))) {
				  
		#ifdef DEBUG_MCMC
			  std::cerr << "# Accept" << std::endl;
		#endif
			  delete current; 
			  current = proposal;
			  
			  // count total acceptances
			  ++FleetStatistics::mcmc_acceptance_count;
		}
		else {
			delete proposal; 
		}
			
        // and call on the sample if we meet all our criteria
        callback(current);
		++FleetStatistics::global_sample_count;

		// and finally if we haven't improved then restart
		if(restart>0 && steps_since_improvement > restart){
			steps_since_improvement = 0; // reset the couter
			auto tmp = current->restart();
			delete current;
			current = tmp;
			current->compute_posterior(data);
			best_posterior = current->posterior; // and reset what it means to be the best
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