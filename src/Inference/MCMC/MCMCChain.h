#pragma once 

#include <functional>
#include "Coroutines.h"
#include "MCMCChain.h"
#include "FiniteHistory.h"
#include "Control.h"
#include "OrderedLock.h"

//#define DEBUG_MCMC 1

template<typename HYP> 
class MCMCChain {
	// An MCMC chain object. 
	// NOTE: This defaultly loads its steps etc from the Fleet command line args
	// so we don't have to pass those in 
	// should be thread safe for modifications to temperature and current 
	
public:
	
	HYP current;
	
	// It's a little important that we use an OrderedLock, because otherwise we have
	// no guarantees about chains accessing in a FIFO order. Non-FIFO is especially
	// bad for ParallelTempering, where there are threads doing the adaptation etc. 
	mutable OrderedLock current_mutex; 
	
	typename HYP::data_t* data;
	
	double maxval;
	
	unsigned long samples; // total number of samples we've done
	unsigned long proposals;
	unsigned long acceptances;
	unsigned long steps_since_improvement; 
	
	std::atomic<double> temperature; // make atomic b/c ParallelTempering may try to change 
	
	FiniteHistory<bool> history;
	
	MCMCChain(HYP& h0, typename HYP::data_t* d) : 
			current(h0), data(d), maxval(-infinity), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}
	
	MCMCChain(HYP&& h0, typename HYP::data_t* d) : 
			current(h0), data(d), maxval(-infinity),
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}

	MCMCChain(const MCMCChain& m) :
		current(m.current), data(m.data), maxval(m.maxval),
		samples(m.samples), proposals(m.proposals), acceptances(m.acceptances), 
		steps_since_improvement(m.steps_since_improvement)	{
			temperature = (double)m.temperature;
			history     = m.history;
		
	}
	MCMCChain(MCMCChain&& m) {
		current = m.current;
		data = m.data;
		maxval = m.maxval;
		samples = m.samples;
		proposals = m.proposals;
		acceptances = m.acceptances;
		steps_since_improvement = m.steps_since_improvement;
		
		temperature = (double)m.temperature;
		history = std::move(m.history);		
	}
	
	virtual ~MCMCChain() { }
	
	/**
	 * @brief Set this data
	 * @param d - what data to set
	 * @param recompute_posterior - should I recompute the posterior on current?
	 */
	void set_data(typename HYP::data_t* d, bool recompute_posterior=true) {
		data = d;
		if(recompute_posterior) {
			current.compute_posterior(*data);
		}
	}
	
	HYP& getCurrent() {
		/**
		 * @brief get a reference to the current value
		 * @return 
		 */		
		return current; 
	}
	
	void runOnCurrent() {
		/**
		 * @brief This is called by the constructor to compute the posterior. NOTE it does not callback
		 */
		
		std::lock_guard guard(current_mutex);
		current.compute_posterior(*data);
		++FleetStatistics::global_sample_count;
		++samples;
	}
	
	
	const HYP& getMax() { 
		return maxval; 
	} 
	
	/**
	 * @brief Run MCMC according to the control parameters passed in.
	 * 		  NOTE: ctl cannot be passed by reference. 
	 * @param ctl
	 */	
	 generator<HYP&> run(Control ctl) {

		assert(ctl.nthreads == 1 && "*** You seem to have called MCMCChain with nthreads>1. This is not how you parallel. Check out ChainPool"); 
		
		#ifdef DEBUG_MCMC
		DEBUG("# Starting MCMC Chain on\t", current.posterior, current.prior, current.likelihood, current.string());
		#endif 
		
		// I may have copied its start time from somewhere else, so change that here
		ctl.start();		
		while(ctl.running()) {
			
			if(current.posterior > maxval) { // if we improve, store it
				maxval = current.posterior;
				steps_since_improvement = 0;
			}
			else { // else keep track of how long
				steps_since_improvement++;
			}
			
			// if we haven't improved
			if(ctl.restart>0 and steps_since_improvement > ctl.restart){
				[[unlikely]];
				
				std::lock_guard guard(current_mutex);
				
				current = current.restart();
				current.compute_posterior(*data);
				
				steps_since_improvement = 0; // reset the couter
				maxval = current.posterior; // and the new max
				
				++samples;
				++FleetStatistics::global_sample_count;

				co_yield current; 
				
				continue;
			}
			
			#ifdef DEBUG_MCMC
			DEBUG("\n# Current\t", data->size(), current.posterior, current.prior, current.likelihood, current.string());
			#endif 
			
			std::lock_guard guard(current_mutex); // lock below otherwise others can modify

			// propose, but restart if we're -infinity
			auto [proposal, fb] = (current.posterior > -infinity ? current.propose() : std::make_pair(current.restart(), 0.0));			
			
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

			#ifdef DEBUG_MCMC
			DEBUG("# Proposed \t", proposal.posterior, proposal.prior, proposal.likelihood, fb, proposal.string());
			#endif 
			
			// use MH acceptance rule, with some fanciness for NaNs
			double ratio = proposal.at_temperature(temperature) - current.at_temperature(temperature) - fb; 		
			if((std::isnan(current.posterior))  or
			   (current.posterior == -infinity) or
			   ((not std::isnan(proposal.posterior)) and
				(ratio >= 0.0 or uniform() < exp(ratio) ))) {
				[[unlikely]];
							
				#ifdef DEBUG_MCMC
				DEBUG("# Accept");
				#endif 
				
				current = std::move(proposal);
  
				history << true;
				++acceptances;
				
			}
			else {
				history << false;
			}

			++samples;
			++FleetStatistics::global_sample_count;
						
			co_yield current;
						
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
