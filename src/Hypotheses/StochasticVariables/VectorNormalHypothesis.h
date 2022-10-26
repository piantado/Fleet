#pragma once 

#include "EigenLib.h"
#include "Interfaces/MCMCable.h"

/**
 * @class VectorNormalHypothesis
 * @author Steven Piantadosi
 * @date 09/08/20
 * @file VectorNormalHypothesis.h
 * @brief This has all of the MCMCable interfaces but just represents n unit Gaussians. This
 * 		  is primarily used in GrammarInference to represent parameters, but it could be used
 *  	  in any other too
 */\
class VectorNormalHypothesis : public MCMCable<VectorNormalHypothesis, void*> {
	// This represents a vector of reals, defaultly here just unit normals. 
	// This gets used in GrammarHypothesis to store both the grammar values and
	// parameters for the model. 
public:
	using self_t = VectorNormalHypothesis;
//	using datum_t = _datum_t;
//	using data_t = _data_t;
	
	double MEAN = 0.0;
	double SD   = 1.0;
	double PROPOSAL_SCALE = 0.20; 
	
	Vector value;
	
	// whether each element of value is constant or not? 
	// This is useful because sometimes we don't want to do MCMC on some parts of the grammar
	std::vector<bool> can_propose; 
	
	VectorNormalHypothesis() {}
	VectorNormalHypothesis(int n) { set_size(n); }
	
	double operator()(int i) const {
		// So we can treat this hypothesis like a vector
		return value(i); 
	}
	
	void set(int i, double v) {
		value(i) = v;
	}
	
	/**
	 * @brief Set whether we can propose to each element of b or not
	 * @param i
	 */	
	void set_can_propose(size_t i, bool b) {
		can_propose[i] = b;
		
		// let's just check they're not all can't propose
		if(std::all_of(can_propose.begin(), can_propose.end(), [](bool v) { return not v; })) {
			assert(false && "*** Cannot set everything to no-propose or else Vector[Half]NormalHypothesis breaks! (loops infinitely on propose)");
		}
		
	}
	
	void set_size(size_t n) {
		value = Vector::Zero(n);
		can_propose.resize(n,true);
	}
	
	size_t size() const {
		return value.size();
	}
	
	virtual double compute_prior() override {
		// Defaultly a unit normal 
		this->prior = 0.0;
		
		for(auto i=0;i<value.size();i++) {
			this->prior += normal_lpdf((double)value(i), this->MEAN, this->SD);
		}
		
		return this->prior;
	}
	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		throw YouShouldNotBeHereError("*** Should not call likelihood here");
	}
	
	
	virtual std::optional<std::pair<self_t,double>> propose() const override {
		self_t out = *this; 
		
		// choose an index
		// (NOTE -- if can_propose is all false, this might loop infinitely...)
		// but we should have caught that up in set_can_propose
		size_t i;
		do {
			i = myrandom(value.size()); 
		} while(!can_propose.at(i));
		
		// propose to one coefficient w/ SD of 0.1
		out.value(i) = value(i) + PROPOSAL_SCALE*random_normal();
		
		// everything is symmetrical so fb=0
		return std::make_pair(out, 0.0);	
	}
	
	virtual self_t restart() const override {
		self_t out = *this;
		for(auto i=0;i<value.size();i++) {
			if(out.can_propose.at(i)) {// we don't want to change parameters unless we can propose to them
				out.value(i) = MEAN + SD*random_normal();
			}
		}
		return out;
	}
	
	virtual size_t hash() const override {
		if(value.size() == 0) return 0; // hmm what to do here?
		
		size_t output = std::hash<double>{}(value(0)); 
		for(auto i=1;i<value.size();i++) {
			hash_combine(output, std::hash<double>{}(value(i)));
		}
		return output;
	}
	
	virtual bool operator==(const self_t& h) const override {
		return value.size() == h.value.size() and value == h.value;
	}
	
	virtual std::string string(std::string prefix="") const override {
		std::string out = prefix+"<";
		for(auto i=0;i<value.size();i++) {
			out += str(value(i))+",";
		}
		out.erase(out.size()-1);
		out += ">";
		return out; 
	}
};

