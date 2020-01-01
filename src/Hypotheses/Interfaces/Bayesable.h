#pragma once 

#include "Datum.h"
#include "IO.h"

// Just a clean interface to define what kinds of operations MCMC requires
// This also defines class types for data
// This also defines interfaces for storing hypotheses (equality, hash, comparison, etc)
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
		// necessary for inserting into big collections not by prior
		prior = 0.0;
		likelihood = 0.0;
		posterior = 0.0;
	}
	virtual double compute_prior() = 0; 
	virtual double compute_single_likelihood(const t_datum& datum) = 0;
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
		// NOTE: Posterior *always* stores at temperature 1
		// if we specify breakout,  when break when prior+likelihood is less than breakout
		//                         (NOTE: this is different than compute_likelihood, which breaks only on likelihood)
		
		++FleetStatistics::posterior_calls; // just keep track of how many calls 
		
		// Always compute a prior
		prior = compute_prior();
		
		// if the prior is -inf, then 
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
		// temperature here applies to the likelihood only
		return prior + likelihood/t;
	}
	
	virtual size_t hash() const=0;
	
	virtual bool operator<(const Bayesable<t_datum,t_data>& l) const {
		// when we implement this, we defaulty sort by posterior (so that TopN works)
		// But we need to be careful because std::set also uses this to determine equality
		// This first checks  postersios, then priors (to handle -inf likelihoods well)
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