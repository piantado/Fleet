#pragma once 

#include <functional>

#include "MCMCChain.h"
#include "FiniteHistory.h"

//#define DEBUG_MCMC 1

// we take callback as a type (which hopefully can be deduce) so we can pass any callable object as callback (like a TopN)
// This must be stored as a shared_ptr 
template<typename HYP, typename callback_t> 
class MCMCChain {
	// An MCMC chain object. 
	// NOTE: This defaultly loads its steps etc from the Fleet command line args
	// so we don't have to pass those in 
	// should be thread safe for modifications to temperature and current 
	
public:
	
	HYP current;
	mutable std::mutex current_mutex; // for access in parallelTempering
	typename HYP::t_data* data;
	
	HYP themax;
	callback_t* callback; // we don't want a shared_ptr because we don't want this deleted, it's owned elsewhere
	unsigned long restart;
	bool          returnmax; 
	
	unsigned long samples; 
	unsigned long proposals;
	unsigned long acceptances;
	unsigned long steps_since_improvement; 
	
	std::atomic<double> temperature; // make atomic b/c ParallelTempering may try to change 
	
	FiniteHistory<bool> history;
	
	MCMCChain(HYP& h0, typename HYP::t_data* d, callback_t& cb ) : 
			current(h0), data(d), themax(), callback(&cb), restart(mcmc_restart), 
			returnmax(true), samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
	}
	
	MCMCChain(HYP&& h0, typename HYP::t_data* d, callback_t& cb ) : 
			current(std::move(h0)), data(d), themax(), callback(&cb), restart(mcmc_restart), 
			returnmax(true), samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
	}

	MCMCChain(HYP& h0, typename HYP::t_data* d, callback_t* cb=nullptr ) : 
			current(h0), data(d), themax(), callback(cb), restart(mcmc_restart), 
			returnmax(true), samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
	}
	
	MCMCChain(HYP&& h0, typename HYP::t_data* d, callback_t* cb=nullptr) : 
			current(std::move(h0)), data(d), themax(), callback(cb), restart(mcmc_restart), 
			returnmax(true), samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
	}

	MCMCChain(const MCMCChain& m) :
		current(m.current), data(m.data), themax(m.themax), callback(m.callback), restart(m.restart),
		returnmax(m.returnmax), samples(m.samples), proposals(m.proposals), acceptances(m.acceptances), 
		steps_since_improvement(m.steps_since_improvement)	{
			temperature = (double)m.temperature;
			history     = m.history;
		
	}
	MCMCChain(MCMCChain&& m) {
		current = std::move(m.current);
		data = m.data;
		themax = std::move(m.themax);
		callback = m.callback;
		restart = m.restart;
		returnmax = m.returnmax;
		samples = m.samples;
		proposals = m.proposals;
		acceptances = m.acceptances;
		steps_since_improvement = m.steps_since_improvement;
		
		temperature = (double)m.temperature;
		history = std::move(m.history);		
	}
	
	virtual ~MCMCChain() { }
	
	HYP& getCurrent() {	return current; }
	
	const HYP& getMax() { return themax; } 
	
	void run(unsigned long steps, unsigned long time) {
		
		using clock = std::chrono::high_resolution_clock;
		
		// compute the info for the curent
		current.compute_posterior(*data);
		
		if(callback != nullptr) (*callback)(current);
		++FleetStatistics::global_sample_count;
		themax = current;
		
		// we'll start at 1 since we did 1 callback on current to begin
		auto start_time = clock::now();
		for(unsigned long i=1; (i<steps || steps==0) && !CTRL_C; i++){
			
			// check the elapsed time 
			if(time > 0 && time_since(start_time) > time) {
				break;
			}
			
			if(thin > 0 and FleetStatistics::global_sample_count % thin == 0) {
				current.print();
			}
			
			
#ifdef DEBUG_MCMC
	COUT "\n# Current\t" << current.posterior TAB current.prior TAB current.likelihood TAB "\t" TAB current.string() ENDL;
#endif
			
			// generate the proposal -- defaulty "restarting" if we're currently at -inf
			HYP proposal; double fb = 0.0;
			
			std::lock_guard guard(current_mutex); // lock below otherwise others can modify

			if(current.posterior > -infinity) {
				std::tie(proposal,fb) = current.propose();
			}
			else {
				proposal = current.restart();
			}
				
			++proposals;
			
			// A lot of proposals end up with the same function, so if so, save time by not
			// computing the posterior
			if(proposal == current) {
				// copy all the properties
				proposal.posterior  = current.posterior;
				proposal.prior      = current.prior;
				proposal.likelihood = current.likelihood;
			}
			else {
				proposal.compute_posterior(*data);
			}

#ifdef DEBUG_MCMC
	COUT "# Proposed \t" << proposal.posterior TAB proposal.prior TAB proposal.likelihood TAB fb TAB proposal.string() ENDL;
#endif
			
			// keep track of the max if we are supposed to
			if(proposal.posterior > themax.posterior) {
				themax = proposal;
				steps_since_improvement = 0;
			}
						
			// use MH acceptance rule, with some fanciness for NaNs
			double ratio = proposal.at_temperature(temperature) - current.at_temperature(temperature) - fb; // Remember: don't just check if proposal->posterior>current->posterior or all hell breaks loose		
			if(   (std::isnan(current.posterior))  or
				  (current.posterior == -infinity) or
					((!std::isnan(proposal.posterior)) and
					 (ratio > 0 or uniform() < exp(ratio)))) {
				
#ifdef DEBUG_MCMC
	  COUT "# Accept" << std::endl;
#endif
				
				current = proposal;
  
				history << true;
				++acceptances;
			}
			else {
				history << false;
			}
				
			// and call on the sample if we meet all our criteria
			if(callback != nullptr) (*callback)(current);
				
			++samples;
			++FleetStatistics::global_sample_count;

			// and finally if we haven't improved then restart
			if(restart>0 && steps_since_improvement > restart){
				steps_since_improvement = 0; // reset the couter
				current = current.restart();
				current.compute_posterior(*data);
				if(callback != nullptr) (*callback)(current);
				if(current.posterior > themax.posterior) {
					themax = current; 	
				}
			}
			
		} // end the main loop	
		
		
		
	}
	void run() { run(0,0); } // run forever

	double acceptance_ratio() {
		return history.mean();
	}
	
	double at_temperature(double t){
		return current.at_temperature(t);
	}
	
};

template<typename HYP>
std::function<void(HYP&)> null_callback = [](HYP&){};
