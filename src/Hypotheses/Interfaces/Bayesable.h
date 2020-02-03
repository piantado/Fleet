#pragma once 

#include "Datum.h"
#include "IO.h"


/**
 * @class Bayesable
 * @author steven piantadosi
 * @date 29/01/20
 * @file Bayesable.h
 * @brief The Bayesable class provides an interface for hypotheses that support Bayesian inference (e.g. computing priors, likelihoods, and posteriors)
 * 		  Note that this class stores prior, likelihood, posterior always at temperature 1.0, and you can get the values of the 
 * 		  posterior at other temperatures via Bayesable.at_temperature(double t)
 */

template<typename _t_datum, typename _t_data=std::vector<_t_datum>>
class Bayesable {
public:
	
	// We'll define a vector of pairs of inputs and outputs
	// this may be used externally to define what data is
	typedef _t_datum t_datum;
	typedef _t_data  t_data; 

	// log values for all
	double prior; 
	double likelihood;
	double posterior; // Posterior always stores at temperature 1
	uintmax_t born; // what count were you born at?

	Bayesable() : prior(NaN), likelihood(NaN), posterior(NaN), born(++FleetStatistics::hypothesis_births) {	}
		
	virtual void clear_bayes() {
		/**
		 * @brief Zero by prior, likelihood, posterior
		 */
		
		// necessary for inserting into big collections not by prior
		prior = 0.0;
		likelihood = 0.0;
		posterior = 0.0;
	}
	
	/**
	 * @brief Compute the prior -- defaultly not defined
	 */
	virtual double compute_prior() = 0; 	
	
	/**
	 * @brief Compute the likelihood of a single data point
	 * @param datum
	 */	
	virtual double compute_single_likelihood(const t_datum& datum) = 0;
	
	/**
	 * @brief Compute the likelihood of a collection of data, by calling compute_single_likelihood on each.
	 * 		  This stops if our likelihood falls below breakout
	 * @param data
	 * @param breakout
	 * @return 
	 */	
	virtual double compute_likelihood(const t_data& data, const double breakout=-infinity) {

		
		// include this in case a subclass overrides to make it non-iterable -- then it must define its own likelihood
		if constexpr (is_iterable_v<t_data>) { 
			
			// defaultly a sum over datums in data (e.g. assuming independence)
			likelihood = 0.0;
			for(const auto& d : data) {
				likelihood += compute_single_likelihood(d);
				if(likelihood == -infinity or std::isnan(likelihood)) break; // no need to continue
				
				// This is a breakout in case our ll is too low
				if(likelihood < breakout) {
					likelihood = -infinity; // should not matter what value, but let's make it -infinity
					break;
				}
				
			}
			return likelihood;		
		}
		else {
			// should never execute this because it must be defined
			assert(0 && "*** If you use a non-iterable t_data, then you must define compute_likelihood on your own."); 
		}
	}


	virtual double compute_posterior(const t_data& data, const double breakout=-infinity) {
		/**
		 * @brief Compute the posterior, by calling prior and likelihood. 
		 * 		  This involves only a little bit of fanciness, which is that if our prior is -inf, then
		 *        we don't both computing the likelihood. 
		 * @param data
		 * @param breakout
		 * @return 
		 */
		
		++FleetStatistics::posterior_calls; // just keep track of how many calls 
		
		// Always compute a prior
		prior = compute_prior();
		
		// if the prior is -inf, then we don't have to compute likelihood
		if(prior == -infinity) {
			likelihood = NaN;
			posterior = -infinity;
		}
		else {		
			likelihood = compute_likelihood(data, breakout-prior);
			posterior = prior + likelihood;	
		}
		
		return posterior;
	}
	
	virtual double at_temperature(double t) {
		/**
		 * @brief Return my posterior score at a given (likelihood) temperature
		 * @param t
		 * @return 
		 */
		
		// temperature here applies to the likelihood only
		return prior + likelihood/t;
	}
	
	/**
	 * @brief Default hash function
	 */	
	virtual size_t hash() const=0;
	
	virtual bool operator<(const Bayesable<t_datum,t_data>& l) const {
		/**
		 * @brief Allow sorting of Bayesable hypotheses. We defaultly sort by posterior so that TopN works right. 
		 * 		  But we also need to be careful because std::set uses this to determine equality, so this
		 *        also checks priors and then hashes. 
		 * @param l
		 * @return 
		 */		
		
		if(posterior < l.posterior) {
			return true;
		}
		else if(l.posterior < posterior) {
			return false;
		}
		else if(prior < l.prior) {
			return true;
		}
		else if(l.prior < prior) {
			return false;
		}
		else {
			// we could be equal, but we only want to say that if we are. 
			// otherwise this is decided by the hash function 
			// so we must ensure that the hash function is only equal for equal values
			return this->hash() < l.hash();
		}
	}
	
	virtual std::string string() const = 0; // my subclasses must implement string
	
	virtual void print(std::string prefix="") {
		/**
		 * @brief Default printing of a hypothesis includes its posterior, prior, likelihood, and quoted string version 
		 * @param prefix
		 */
		
		std::lock_guard guard(Fleet::output_lock);
		// TODO: Include  this->born  once that is updated correctly
		COUT prefix << this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->string()) ENDL;		
	}
};



template<typename _t_datum, typename _t_data>
std::ostream& operator<<(std::ostream& o, Bayesable<_t_datum,_t_data>& x) {
	o << x.string();
	return o;
}