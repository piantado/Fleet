#pragma once 

#include <functional>

#include "MCMCChain.h"
#include "FiniteHistory.h"
#include "Control.h"

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
	typename HYP::data_t* data;
	
	double maxval;
	callback_t* callback; // we don't want a shared_ptr because we don't want this deleted, it's owned elsewhere
	
	unsigned long samples; // total number of samples (callbacks) we've done
	unsigned long proposals;
	unsigned long acceptances;
	unsigned long steps_since_improvement; 
	
	std::atomic<double> temperature; // make atomic b/c ParallelTempering may try to change 
	
	FiniteHistory<bool> history;
	
	MCMCChain(HYP& h0, typename HYP::data_t* d, callback_t& cb ) : 
			current(h0), data(d), maxval(-infinity), callback(&cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}
	
	MCMCChain(HYP&& h0, typename HYP::data_t* d, callback_t& cb ) : 
			current(h0), data(d), maxval(-infinity), callback(&cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}

	MCMCChain(HYP& h0, typename HYP::data_t* d, callback_t* cb=nullptr ) : 
			current(h0), data(d), maxval(-infinity), callback(cb), 
			samples(0), proposals(0), acceptances(0), steps_since_improvement(0),
			temperature(1.0), history(100) {
			runOnCurrent();
	}
	
	MCMCChain(HYP&& h0, typename HYP::data_t* d, callback_t* cb=nullptr) : 
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
	
	/**
	 * @brief Run MCMC according to the control parameters passed in.
	 * 		  NOTE: ctl cannot be passed by reference. 
	 * @param ctl
	 */	
	 void run(Control ctl) {

		assert(ctl.nthreads == 1 && "*** You seem to have called MCMCChain with nthreads>1. This is not how you paralle. Check out ChainPool"); 
		
		#ifdef DEBUG_MCMC
		DEBUG("# Starting MCMC Chain on\t", current.posterior, current.prior, current.likelihood, current.string());
		#endif 
		
		// I may have copied its start time from somewhere else, so change that here
		ctl.start();
		
		while(ctl.running()) {
			
			if(current.posterior > maxval) {
				maxval = current.posterior;
				steps_since_improvement = 0;
			}
			
			// if we haven't improved
			if(ctl.restart>0 and steps_since_improvement > ctl.restart){
				steps_since_improvement = 0; // reset the couter
				
				std::lock_guard guard(current_mutex);
				
				current = current.restart();
				current.compute_posterior(*data);
				
				++samples;
				++FleetStatistics::global_sample_count;

				if(callback != nullptr and samples >= ctl.burn) {
					(*callback)(current);
				}
				
				continue;
			}
			
			
			#ifdef DEBUG_MCMC
			DEBUG("\n# Current\t", data->size(), current.posterior, current.prior, current.likelihood, current.string());
			#endif 
			
			std::lock_guard guard(current_mutex); // lock below otherwise others can modify

//			extern unsigned long thin;
//			if(thin > 0 and FleetStatistics::global_sample_count % thin == 0) {
//				current.print();
//			}

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
			
//			CERR "MCMC Chain U = " << uniform() ENDL;
			// use MH acceptance rule, with some fanciness for NaNs
			double ratio = proposal.at_temperature(temperature) - current.at_temperature(temperature) - fb; 		
			if((std::isnan(current.posterior))  or
			   (current.posterior == -infinity) or
			   ((not std::isnan(proposal.posterior)) and
				(ratio >= 0.0 or uniform() < exp(ratio) ))) {
							
				#ifdef DEBUG_MCMC
				DEBUG("# Accept");
				#endif 
				
				current = std::move(proposal);
  
				history << true;
				++acceptances;
				
				[[unlikely]];
			}
			else {
				history << false;
			}
				
			// and call on the sample if we meet all our criteria
			if(callback != nullptr and samples >= ctl.burn and
				(ctl.thin == 0 or FleetStatistics::global_sample_count % ctl.thin == 0)) {
				(*callback)(current);
			}
			
			if(ctl.print > 0 and FleetStatistics::global_sample_count % ctl.print == 0) {
				current.print();
			}
			
//			if( FleetStatistics::global_sample_count>0 and FleetStatistics::global_sample_count % 100 == 0 ) {
//				CERR "# Acceptance rate: " TAB double(acceptances)/double(proposals) ENDL;
//			}
				
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
