#pragma once 

#include <utility>
#include <iomanip>
#include <signal.h>

#include "FleetArgs.h"
#include "Errors.h"
#include "Datum.h"
#include "IO.h"
#include "Statistics/FleetStatistics.h"
#include "Miscellaneous.h"

// if we define this, then we won't use breakouts
//#define NO_BREAKOUT 1

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
	virtual size_t hash() const=0;
	virtual std::string string(std::string prefix="") const = 0; 
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
				
				
				auto sll = compute_single_likelihood(d);
				
				likelihood += sll;
				
				if(likelihood == -infinity or std::isnan(likelihood)) break; // no need to continue
				
				// This is a breakout in case our ll is too low
				if(FleetArgs::LIKELIHOOD_BREAKOUT){
					assert((sll <= 0 or breakout==-infinity) && "*** Cannot use breakout if likelihoods are positive");
					if(likelihood < breakout) {
						return likelihood = -infinity; // should not matter what value, but let's make it -infinity
					}
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
	 * 		  This includes only a little bit of fanciness, which is that if our prior is -inf, then
	 *        we don't both computing the likelihood. 
	 * 
	 * 		  To understand breakout, look at MCMCChain.h and see how compute_posterior is called -- the first
	 * 		  term in breakout is (u + current.at_temperature(temperature) + fb), and the second is the temperature
	 * 		  We have to pass in two pieces because the temperature only applies ot the likelihood.
	 * 		  Note that when we use breakout in compute_likelihood, it is only for the likelihood temp.
	 * 
	 * 		  NOTE: The order here is fixed to compute the prior first. This permits us to penalize the "prior"
	 * 		  with something in the likelihood -- for instance if we want to use a runtime prior that is computed
	 * 		  as we run the likelihood
	 * @param data
	 * @param breakout
	 * @return 
	 */
	virtual double compute_posterior(const data_t& data, const std::pair<double,double> breakoutpair=std::make_pair(-infinity,1.0)) {
		
		++FleetStatistics::posterior_calls; // just keep track of how many calls 
		
		// Always compute a prior
		prior = compute_prior();
		
		// if the prior is -inf, then we don't have to compute likelihood
		if(prior == -infinity) {
			likelihood = NaN;
			posterior = -infinity;
		}
		else {		
			// NOTE: here we *subtract* off prior so the breakout in compute_likelihood is ONLY for the likelihood
			auto [b,t] = breakoutpair; // see MCMCChain.h to understand this and hte next line
			likelihood = compute_likelihood(data, (b-prior)*t);
			posterior = prior + likelihood;	
		}
		
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
	
	// Defaultly we search by posterior 
	auto operator<=>(const Bayesable<datum_t,data_t>& other) const {
		/**
		 * @brief Allow sorting of Bayesable hypotheses. We defaultly sort by posterior so that TopN works right. 
		 * 		  But we also need to be careful because std::set uses this to determine equality, so this
		 *        also checks priors and then finally hashes (which should represent values). 
		 * @param l
		 * @return 
		 */		
		
		if(posterior != other.posterior) {
			return fp_ordering(posterior, other.posterior); 
		}
		else if(prior != other.prior) {
			return fp_ordering(prior,other.prior); 
		}
		else {
			// we could be equal, but we only want to say that if we are. 
			// otherwise this is decided by the hash function 
			// so we must ensure that the hash function is only equal for equal values
			return this->hash() <=> other.hash(); 
		}
	}
	
	virtual void show(std::string prefix="") {
		/**
		 * @brief Default printing of a hypothesis includes its posterior, prior, likelihood, and quoted string version 
		 * @param prefix
		 */
		if(prefix != "") {
			print(prefix, this->posterior, this->prior, this->likelihood, QQ(this->string())); 
		}
		else {
			print(this->posterior, this->prior, this->likelihood, QQ(this->string())); 
		}
	}
};



template<typename _datum_t, typename _data_t>
std::ostream& operator<<(std::ostream& o, Bayesable<_datum_t,_data_t>& x) {
	o << x.string();
	return o;
}


// little helper functions 
template<typename HYP>
std::function get_posterior = [](const HYP& h) {return h.posterior; };

template<typename HYP>
std::function get_prior = [](const HYP& h) {return h.prior; };

template<typename HYP>
std::function get_likelihood = [](const HYP& h) {return h.likelihood; };


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