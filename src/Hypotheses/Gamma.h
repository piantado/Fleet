
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
	
	
	virtual double compute_likelihood(const data_t data, const double breakout=-infinity) override {
		return 0.0; // throw YouShouldNotBeHereError("*** Should not call likelihood here");
	}
	

	virtual std::optional<std::pair<self_t,double>> propose() const override {
		
		// choose a scaling factor
		float s = random_gamma(PROPOSAL_K, PROPOSAL_K); 
		
		auto out = *this; // copy myself

		// here our proposal is a scaling so we need to treat it as a change of variables
		// and include the jacobian, which is ds/dx' = 1/x. The reverse is ds/sx=1/x', 
		// so in the acceptance ratio we have x/x' which is the same as 1/s
		out.value = out.value * s; // set the value
		
		// force a hard boundary
		if(out.value < 1e-6) 
			out.value = 1e-6;
			
		// NOTE that gamma_lpdf here is with shape/rate unlike random_Gamma above
		// here, the forward-backward simplifies to -log(s)
		return std::make_pair(out, -log(s)); 
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


