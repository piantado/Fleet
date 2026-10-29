#pragma once 

#include "EigenLib.h"
#include "Interfaces/MCMCable.h"

/**
 * @class TNormalVariable
 * @author Steven Piantadosi
 * @date 11/09/20
 * @file TNormalVariable.h
 * @brief A TNormalVariable encapsulates MCMC operations on a single
 * 		  real number with a standard normal prior. This is useful for variables in e.g. GrammarInference.
 * 		  This has a normal prior on a parameter and then allows an output transformation, which is 
 * 		  cached for speed
 */
template<float (*f)(float) > // transform set at compile time
class TNormalVariable : public MCMCable<TNormalVariable<f>, void*> {
public:
	using self_t = TNormalVariable<f>;
	using data_t = typename MCMCable<TNormalVariable<f>, void*>::data_t;
	
	float MEAN = 0.0;
	float SD   = 1.0;
	float PROPOSAL_SCALE = 0.250; 
	
	bool can_propose;
	
	float value;
	float fvalue; // transformed variable 
	
	TNormalVariable() : can_propose(true) {
		//set(0.0); // just so it doesn't start insane
 	}
	
	// Set the value (via float or double)
	// NOTE: This ses the PRE-transformed value
	template<typename T>
	void set_untransformed(const T v) {
		if(v != value) {
			value = v;
			fvalue = f(v);
		}
	}
	
	float get_untransformed() {
		return value; 
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
		return 0.0; // this is here so we can run MCMC with no data
//		throw YouShouldNotBeHereError("*** Should not call likelihood here");
	}
	
	virtual std::optional<std::pair<self_t,double>> propose() const override {
		if(not can_propose) return {};
		
		self_t out = *this;
		out.set_untransformed(value + PROPOSAL_SCALE*random_normal());
		return std::make_pair(out, 0.0); // everything is symmetrical so fb=0
			
	}
	
	virtual self_t restart() const override {
		self_t out = *this;
		if(can_propose) {
			out.set_untransformed(MEAN + SD*random_normal());
		}
		else {
			// should already have just copied it anyways
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
		return prefix+"TN<"+str(value)+">";
	}
	
};


using UniformVariable     = TNormalVariable< +[](float x)->float { return normal_cdf<float>(x, 0.0, 1.0); }>;

using ExponentialVariable = TNormalVariable< +[](float x)->float { return -log(normal_cdf<float>(-x, 0.0, 1.0)); }>;
