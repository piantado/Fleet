#pragma once 

#include "EigenLib.h"
#include "Interfaces/MCMCable.h"

/**
 * @class VectorHalfNormalHypothesis
 * @author Steven Piantadosi
 * @date 13/07/21
 * @file VectorNormalHypothesis.h
 * @brief Distribution on half-normals. Man, it was a pain to try to make this inherit from VectorNormalHypothesis
 */

class VectorHalfNormalHypothesis : public MCMCable<VectorHalfNormalHypothesis, void*> {
	// This represents a vector of reals, defaultly here just unit normals. 
	// This gets used in GrammarHypothesis to store both the grammar values and
	// parameters for the model. 
public:
	using self_t = VectorHalfNormalHypothesis;
	
	double MEAN = 0.0;
	double SD   = 1.0;
	double PROPOSAL_SCALE = 0.20; 
	
	Vector value;
	
	// whether each element of value is constant or not? 
	// This is useful because sometimes we don't want to do MCMC on some parts of the grammar
	std::vector<bool> can_propose; 
	
	VectorHalfNormalHypothesis() {}
	VectorHalfNormalHypothesis(int n) { set_size(n); }
	
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
	
	virtual double compute_prior() override {
		// Defaultly a unit normal 
		this->prior = 0.0;
		
		for(auto i=0;i<value.size();i++) {
			this->prior += normal_lpdf((double)value(i), this->MEAN, this->SD) + LOG2; // because it is only positive
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
		size_t i;
		do {
			i = myrandom(this->value.size()); 
		} while(!this->can_propose[i]);
		
		// propose to one coefficient w/ SD of 0.1
		// note the abs here -- 
		out.value(i) = abs(this->value(i) + this->PROPOSAL_SCALE*random_normal());
		
		// fb now has to sum over both directions -- I could have made the short proposal, or the long one
		// that wraps around 0 
		double f = logplusexp( normal_lpdf((double)abs(this->value(i)-out.value(i)), 0.0, this->PROPOSAL_SCALE),
							   normal_lpdf((double)    this->value(i)+out.value(i),  0.0, this->PROPOSAL_SCALE));
		
		double b = logplusexp( normal_lpdf((double)abs(out.value(i)-this->value(i)), 0.0, this->PROPOSAL_SCALE),
							   normal_lpdf((double)    out.value(i)+this->value(i),  0.0, this->PROPOSAL_SCALE));
		
		// everything is symmetrical so fb=0
		return std::make_pair(out, f-b);	
	}
	
	virtual self_t restart() const override {
		self_t out = *this;
		for(auto i=0;i<this->value.size();i++) {
			if(out.can_propose[i]) {
				// we don't want to change parameters unless we can propose to them
				out.value(i) = abs(this->MEAN + this->SD*random_normal());
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
		std::string out = prefix+"NV<";
		for(auto i=0;i<value.size();i++) {
			out += str(value(i));
		}
		out += ">";
		return out; 
	}
};


/**
 * @class __VectorHalfNormalHypothesis
 * @author Steven Piantadosi
 * @date 12/07/21
 * @file VectorNormalHypothesis.h
 * @brief Half-normal hypothese are bounded at zero, normal proposals wrap around
 */
//template<typename self_t>
//class __VectorHalfNormalHypothesis : public __VectorNormalHypothesis<self_t> {
//public:
//	using Super = __VectorNormalHypothesis<self_t>;
//	
//	virtual double compute_prior() override {
//		return this->prior = Super::compute_prior() + LOG2*this->value.size(); /// twice the probability
//	}
//	
//	std::pair<self_t,double> propose() const override {
//		self_t out = *this;
//		
//		// choose an index
//		// (NOTE -- if can_propose is all false, this might loop infinitely...)
//		size_t i;
//		do {
//			i = myrandom(this->value.size()); 
//		} while(!this->can_propose[i]);
//		
//		// propose to one coefficient w/ SD of 0.1
//		// note the abs here -- 
//		out.value(i) = abs(this->value(i) + this->PROPOSAL_SCALE*random_normal());
//		
//		// fb now has to sum over both directions -- I could have made the short proposal, or the long one
//		// that wraps around 0 
//		double f = logplusexp( normal_lpdf((double)abs(this->value(i)-out.value(i)), 0.0, this->PROPOSAL_SCALE),
//							   normal_lpdf((double)    this->value(i)+out.value(i),  0.0, this->PROPOSAL_SCALE));
//		
//		double b = logplusexp( normal_lpdf((double)abs(out.value(i)-this->value(i)), 0.0, this->PROPOSAL_SCALE),
//							   normal_lpdf((double)    out.value(i)+this->value(i),  0.0, this->PROPOSAL_SCALE));
//		
//		// everything is symmetrical so fb=0
//		return std::make_pair(out, f-b);	
//	}
//	
//	virtual self_t restart() const override {
//		self_t out = *this;
//		for(auto i=0;i<this->value.size();i++) {
//			if(out.can_propose[i]) {
//				// we don't want to change parameters unless we can propose to them
//				out.value(i) = abs(this->MEAN + this->SD*random_normal());
//			}
//		}
//		return out;
//	}
//};
