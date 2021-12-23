#pragma once 

#include <iomanip>
#include <signal.h>

#include "Errors.h"
#include "Datum.h"
#include "IO.h"
#include "Statistics/FleetStatistics.h"
#include "Miscellaneous.h"

extern volatile sig_atomic_t CTRL_C;

/**
 * @class Bayesable
 * @author steven piantadosi
 * @date 29/01/20
 * @file Bayesable.h
 * @brief The Bayesable class provides an interface for hypotheses that support Bayesian inference (e.g. computing priors, likelihoods, and posteriors)
 * 		  Note that this class stores prior, likelihood, posterior always at temperature 1.0, and you can get the values of the 
 * 		  posterior at other temperatures via Bayesable.at_temperature(double t)
 */

//cache::lru_cache<size_t, std::tuple<double,double,double>> posterior_cache(10);

template<typename _datum_t, typename _data_t=std::vector<_datum_t>>
class Bayesable {
public:
	
	// We'll define a vector of pairs of inputs and outputs
	// this may be used externally to define what data is
	typedef _datum_t datum_t;
	typedef _data_t  data_t; 

	// log values for all
	double prior; 
	double likelihood;
	double posterior; // Posterior always stores at temperature 1
	
	// some born info/statistics
	uintmax_t born; // what count were you born at?
	size_t born_chain_idx; // if we came from a ChainPool, what chain index were we born on?
	
	Bayesable() : prior(NaN), likelihood(NaN), posterior(NaN), born(++FleetStatistics::hypothesis_births), born_chain_idx(0) {	}
	
	// Stuff for subclasses to implement: 
	
	/**
	 * @brief Default hash function
	 */	
	virtual size_t hash() const=0;
	
	virtual std::string string(std::string prefix="") const = 0; // my subclasses must implement string
	
	/**
	 * @brief Compute the prior -- defaultly not defined
	 */
	virtual double compute_prior() = 0; 	
	
	/**
	 * @brief Compute the likelihood of a single data point
	 * @param datum
	 */	
	virtual double compute_single_likelihood(const datum_t& datum)  {
		throw NotImplementedError();
	}
	
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
	 * @brief Compute the likelihood of a collection of data, by calling compute_single_likelihood on each.
	 * 		  This stops if our likelihood falls below breakout
	 * @param data
	 * @param breakout
	 * @return 
	 */	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) {

		
		// include this in case a subclass overrides to make it non-iterable -- then it must define its own likelihood
		if constexpr (is_iterable_v<data_t>) { 
			
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
				
				// add a break here 
				if(CTRL_C) break; 
			}
			
			return likelihood;		
		}
		else {
			// should never execute this because it must be defined
			throw NotImplementedError("*** If you use a non-iterable data_t, then you must define compute_likelihood on your own."); 
		}
	}

	/**
	 * @brief Compute the posterior, by calling prior and likelihood. 
	 * 		  This involves only a little bit of fanciness, which is that if our prior is -inf, then
	 *        we don't both computing the likelihood. 
	 * 
	 * 		  NOTE: The order here is fixed to compute the prior first. This permits us to penalize the "prior"
	 * 		  with something in the likelihood -- for instance if we want to use a runtime prior that is computed
	 * 		  as we run the likelihood
	 * @param data
	 * @param breakout
	 * @return 
	 */
	virtual double compute_posterior(const data_t& data, const double breakout=-infinity) {
		
		++FleetStatistics::posterior_calls; // just keep track of how many calls 
		
//		auto hsh = this->hash();
//		if(posterior_cache.exists(hsh)) {
//			CERR "HIT" ENDL;
//			auto q = posterior_cache.get(hsh); // TODO: Should also set likelihood and prior
//			this->posterior  = std::get<0>(q);
//			this->prior      = std::get<1>(q);
//			this->likelihood = std::get<2>(q);
//			return posterior;
//		}
		
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
		
//		posterior_cache.put(hsh, std::make_tuple(posterior, prior, likelihood));
		
		return posterior;
	}
	
	virtual double at_temperature(double t) const {
		/**
		 * @brief Return my posterior score at a given (likelihood) temperature
		 * @param t
		 * @return 
		 */
		
		// temperature here applies to the likelihood only
		return prior + likelihood/t;
	}
		
	virtual bool operator<(const Bayesable<datum_t,data_t>& l) const {
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
	
	virtual void print(std::string prefix="") {
		/**
		 * @brief Default printing of a hypothesis includes its posterior, prior, likelihood, and quoted string version 
		 * @param prefix
		 */
		
		std::lock_guard guard(output_lock);
		// TODO: Include  this->born  once that is updated correctly
		COUT std::setprecision(14) << prefix << this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->string()) ENDL;		
	}
};



template<typename _datum_t, typename _data_t>
std::ostream& operator<<(std::ostream& o, Bayesable<_datum_t,_data_t>& x) {
	o << x.string();
	return o;
}




// Interface for std::fmt
//#include "fmt/format.h"
//
//template<typename _datum_t, typename _data_t>
//struct fmt::formatter<Bayesable<_datum_t,_data_t>&> {
//  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
//
//  template <typename FormatContext>
//  auto format(const Bayesable<_datum_t,_data_t>& h, FormatContext& ctx) {
//    return format_to(ctx.out(), "{}", h.string());
//  }
//};