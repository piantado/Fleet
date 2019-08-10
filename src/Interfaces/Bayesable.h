#pragma once 

#include "Datum.h"
#include "IO.h"

// Just a clean interface to define what kinds of operations MCMC requires
// This also defines class types for data
// This also defines interfaces for storing hypotheses (equality, hash, comparison, etc)
template<typename t_input, typename t_output, typename _t_datum=default_datum<t_input, t_output>>
class Bayesable {
public:
	
	// We'll define a vector of pairs of inputs and outputs
	// this may be used externally to define what data is
	typedef _t_datum t_datum;
	typedef std::vector<t_datum> t_data; 

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
	virtual double compute_likelihood(const t_data& data) {
		// defaultly a sum over datums in data (e.g. assuming independence)
		likelihood = 0.0;
		for(const auto& d : data) {
			likelihood += compute_single_likelihood(d);
			if(likelihood == -infinity) break; // no need to continue
		}
		return likelihood;		
	}
	
	virtual double compute_posterior(const t_data& data) {
		// NOTE: Posterior *always* stores at temperature 1
		
		++FleetStatistics::posterior_calls; // just keep track of how many calls 
		
		// Always compute a prior
		prior = compute_prior();
		
		// if the prior is -inf, then 
		if(prior == -infinity) {
			likelihood = NaN;
			posterior = -infinity;
		}
		else {		
			likelihood = compute_likelihood(data);
			posterior = prior + likelihood;	
		}
		
		return posterior;
	}
	
	virtual double at_temperature(double t) {
		// temperature here applies to the likelihood only
		return prior + likelihood/t;
	}
	
	virtual size_t hash() const=0;
	
	virtual bool operator<(const Bayesable<t_input,t_output,t_datum>& l) const {
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



