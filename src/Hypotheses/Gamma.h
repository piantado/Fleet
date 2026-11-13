
#pragma once 

#include "Interfaces/MCMCable.h"


class Gamma : public MCMCable<Gamma, NullDatum_t> {
	// a single gamma random variable
public:
	using self_t = Gamma;
	
	float PROPOSAL_K = 200.0; // 
	
	float alpha = 1;
	float beta = 1; // RATE parameter (not difference to std::gamma_distribution)
	
	float value; 
	bool can_propose = true;

	Gamma(float a, float b) : alpha(a), beta(b), can_propose(true) { 
		value = 1.0; 
	}
	
	virtual double compute_prior() override {
		return this->prior = gamma_lpdf(value, alpha, beta);
	}
	
	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		return 0.0; // throw YouShouldNotBeHereError("*** Should not call likelihood here");
	}
	

	virtual std::optional<std::pair<self_t,double>> propose() const override {
		
		// choose a scaling factor s ~ Gamma(shape K, rate K), which has mean 1
		const double K = PROPOSAL_K;
		const double s = random_gamma(K, K);
		
		auto out = *this; // copy myself
		out.value = (float)((double)value * s); // set the value
		
		// A hard lower boundary. A proposal below it (or one that is not finite and positive) is a
		// FAILED proposal, which MCMC counts as a rejection: that is exact for the target restricted to
		// values >= 1e-6. (This used to clamp the value to 1e-6 while computing fb as if it had not.)
		if(not (out.value >= 1e-6f) or std::isinf(out.value)) 
			return {};
			
		// The proposal is x' = x*s with s ~ g = Gamma(K,K), so the densities are
		//     q(x'|x) = g(x'/x)/x = g(s)/x       and       q(x|x') = g(x/x')/x' = g(1/s)/x'
		// and therefore
		//     fb = log q(x'|x) - log q(x|x') = log g(s) - log g(1/s) + log(s)
		//        = (2K-1) log(s) - K (s - 1/s)
		// NOTE: this used to return -log(s). That is the right answer only for a multiplier that is
		// symmetric in log space (g(s)/g(1/s) = 1/s^2, e.g. a lognormal), which Gamma(K,K) is not:
		// log(s) has mean ~ -1/(2K) and variance ~ 1/K, and ignoring that drift made the chain sample
		// from prior(x)/x instead of prior(x) for every K -- e.g. a Gamma(5,5) prior came out with
		// mean 0.8 instead of 1 (see Testing/TestAudit.cpp).
		const double fb = (2.0*K-1.0)*log(s) - K*(s - 1.0/s);
		return std::make_pair(out, fb); 
	}
	
	virtual self_t restart() const override {
		auto v = *this;
		v.value = random_gamma(alpha,beta); // resamples
		return v;
	}

	virtual size_t hash() const override {
		return std::hash<double>{}(value); 
	}
	
	virtual operator float()  const { return value; }
	virtual operator double() const { return value; }
	
	virtual bool operator==(const self_t& h) const override {
		return value == h.value;
	}
	
	virtual std::string string(std::string prefix="") const override {
		std::string out = prefix+"NV<"+str(value)+">";
		return out; 
	}
};


