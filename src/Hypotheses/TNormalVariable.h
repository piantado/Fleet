#pragma once 

#include "EigenLib.h"
#include "Interfaces/MCMCable.h"

/**
 * @class TNormalVariable
 * @author Steven Piantadosi
 * @date 11/09/20
 * @file VectorHypothesis.h
 * @brief A TNormalVariable encapsulates MCMC operations on a single
 * 		  real number with a standard normal prior. This is useful for variables in e.g. GrammarInference.
 * 		  This has a normal prior on a parameter and then allows an output transformation, which is 
 * 		  cached for speed
 */
template<float (*f)(float) > // transformat set at compile time
class TNormalVariable : public MCMCable<TNormalVariable<f>, void*> {
public:
	using self_t = TNormalVariable<f>;
	using data_t = MCMCable<TNormalVariable<f>, void*>::data_t;
	
	float MEAN = 0.0;
	float SD   = 1.0;
	float PROPOSAL_SCALE = 0.10; 
	
	bool can_propose;
	
	float value;
	float fvalue; // transformed variable 
	
	TNormalVariable() : can_propose(true) {
		//set(0.0); // just so it doesn't start insane
 	}
	
	// Set the value (via float or double)
	template<typename T>
	void set(T v) {
		if(v != value) {
			value = v;
			fvalue = f(v);
		}
	}
	
	/**
	 * @brief  Get interfaces to the transformed variable. 
	 */	
	float get() const {
		return fvalue; 
	}
	
	virtual double compute_prior() override {
		// Defaultly a unit normal 
		return this->prior = normal_lpdf(value, MEAN, SD);
	}
	
	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		throw YouShouldNotBeHereError("*** Should not call likelihood here");
	}
	
	virtual std::pair<self_t,double> propose() const override {
		self_t out = *this;
		
		if(can_propose)
			out.set(value + PROPOSAL_SCALE*normal(rng));
		
		// everything is symmetrical so fb=0
		return std::make_pair(out, 0.0);	
	}
	
	virtual self_t restart() const override {
		self_t out = *this;
		if(can_propose) {
			out.set(MEAN + SD*normal(rng));
		}
		else {
			// should have just copied it anyways
		}
		return out;
	}
	
	virtual size_t hash() const override {
		return std::hash<double>{}(value); 
	}
	
	virtual bool operator==(const self_t& h) const override {
		return value == h.value;
	}
	
	virtual std::string string(std::string prefix="") const override {
		return prefix+"N<"+str(value)+">";
	}
	
};
