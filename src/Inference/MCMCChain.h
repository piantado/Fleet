#pragma once 

#include <functional>

#include "MCMCChain.h"
#include "FiniteHistory.h"

#define DEBUG_MCMC 0

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
	
	double maxval;
	callback_t* callback; // we don't want a shared_ptr because we don't want this deleted, it's owned elsewhere
	
	unsigned long samples; // total number of samples (callbacks) we've done
	unsigned long proposals;
	unsigned long acceptances;
	unsigned long steps_since_improvement; 
	
	std::atomic<double> temperature; // make atomic b/c ParallelTempering may try to change 
	
	Fleet::Statistics::FiniteHistory<bool> history;
	
	MCMCChain(HYP& h0, typename HYP::t_data* d, callback_t& cb ) : 
			current(h0), data(d), maxval(-infinity), callback(&cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}
	
	MCMCChain(HYP&& h0, typename HYP::t_data* d, callback_t& cb ) : 
			current(h0), data(d), maxval(-infinity), callback(&cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}

	MCMCChain(HYP& h0, typename HYP::t_data* d, callback_t* cb=nullptr ) : 
			current(h0), data(d), maxval(-infinity), callback(cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}
	
	MCMCChain(HYP&& h0, typename HYP::t_data* d, callback_t* cb=nullptr) : 
			current(h0), data(d), maxval(-infinity), callback(cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
				
			runOnCurrent();
	}

	MCMCChain(const MCMCChain& m) :
		current(m.current), data(m.data), maxval(m.maxval), callback(m.callback),
		samples(m.samples), proposals(m.proposals), acceptances(m.acceptances), 
		steps_since_improvement(m.steps_since_improvement)	{
			temperature = (double)m.temperature;
			history     = m.history;
		
	}
	MCMCChain(MCMCChain&& m) {
		current = m.current;
		data = m.data;
		maxval = m.maxval;
		callback = m.callback;
		samples = m.samples;
		proposals = m.proposals;
		acceptances = m.acceptances;
		steps_since_improvement = m.steps_since_improvement;
		
		temperature = (double)m.temperature;
		history = std::move(m.history);		
	}
	
	virtual ~MCMCChain() { }
	
	HYP& getCurrent() {
		/**
		 * @brief get a reference to the current value
		 * @return 
		 */
		
		return current; 
	}
	
	void runOnCurrent() {
		/**
		 * @brief This is called by the constructor to compute the posterior and callback for an initial h0
		 */
				
		// updates current and calls callback (in constructor)
		std::lock_guard guard(current_mutex);
		current.compute_posterior(*data);
		if(callback != nullptr) (*callback)(current);
		++FleetStatistics::global_sample_count;
		++samples;
	}
	
	
	const HYP& getMax() { 
		return maxval; 
	} 
	
	void run(Control ctl) {
		/**
		 * @brief Run MCMC according to the control parameters passed in.
		 * 		  NOTE: ctl cannot be passed by reference. 
		 * @param ctl
		 */
		assert(ctl.threads == 1); // this is not how we run parallel 
		
		// I may have copied its start time from somewhere else, so change that here
		ctl.start();
		
		if(DEBUG_MCMC) DEBUG("# Starting MCMC Chain on\t", current.posterior, current.prior, current.likelihood, current.string());
				
		// we'll start at 1 since we did 1 callback on current to begin
		while(ctl.running()) {
			
			if(current.posterior > maxval) {
				maxval = current.posterior;
				steps_since_improvement = 0;
			}
			
			// if we haven't improved
			if(ctl.restart>0 and steps_since_improvement > ctl.restart){
				steps_since_improvement = 0; // reset the couter
				current = current.restart();
				current.compute_posterior(*data);
				
				++samples;
				++FleetStatistics::global_sample_count;

				if(callback != nullptr and samples >= ctl.burn) {
					(*callback)(current);
				}
				
				continue;
			}
			
			
			if(DEBUG_MCMC) DEBUG("\n# Current\t", data->size(), current.posterior, current.prior, current.likelihood, current.string());
			
			std::lock_guard guard(current_mutex); // lock below otherwise others can modify

			if(thin > 0 and FleetStatistics::global_sample_count % thin == 0) {
				current.print();
			}

			// propose, but restart if we're -infinity
			auto [proposal, fb] = current.posterior > -infinity ? current.propose() : std::make_tuple(current.restart(), 0.0);
//			auto [proposal, fb] = current.propose();
			
			
			++proposals;
			
			// A lot of proposals end up with the same function, so if so, save time by not
			// computing the posterior
			if(proposal == current) {
				// copy all the properties
				proposal.posterior  = current.posterior;
				proposal.prior      = current.prior;
				proposal.likelihood = current.likelihood;
				proposal.born       = current.born;
			}
			else {
				proposal.compute_posterior(*data);
			}

			if(DEBUG_MCMC) DEBUG("# Proposed \t", proposal.posterior, proposal.prior, proposal.likelihood, fb, proposal.string());
			
			// use MH acceptance rule, with some fanciness for NaNs
			double ratio = proposal.at_temperature(temperature) - current.at_temperature(temperature) - fb; 		
			if((std::isnan(current.posterior))  or
			   (current.posterior == -infinity) or
			   ((not std::isnan(proposal.posterior)) and
				(ratio >= 0.0 or  uniform() < exp(ratio) ))) {
							
				if(DEBUG_MCMC) DEBUG("# Accept");
								
				current = proposal;
  
				history << true;
				++acceptances;
			}
			else {
				history << false;
			}
				
			// and call on the sample if we meet all our criteria
			if(callback != nullptr and samples >= ctl.burn) {
				(*callback)(current);
			}
				
			++samples;
			++FleetStatistics::global_sample_count;
			
		} // end the main loop	
		
		
		
	}
	void run() { 
		/**
		 * @brief Run forever
		 */
		
		run(Control(0,0)); 
	}

	double acceptance_ratio() {
		/**
		 * @brief Get my acceptance ratio
		 * @return 
		 */
		
		return history.mean();
	}
	
	double at_temperature(double t){
		/**
		 * @brief Return my current posterior at a given temperature t
		 * @param t
		 * @return 
		 */
		
		return current.at_temperature(t);
	}
	
};

template<typename HYP>
std::function<void(HYP&)> null_callback = [](HYP&){};
