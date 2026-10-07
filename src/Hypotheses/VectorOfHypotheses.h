#pragma once

#include <vector>

#include "Interfaces/MCMCable.h"

// Manages proposals etc for vectors of hypotheses
template<typename HYP>
class VectorOfHypotheses : public MCMCable<VectorOfHypotheses<HYP>, NullDatum_t> {
public:
	using this_t = VectorOfHypotheses<HYP>;
	
	std::vector<HYP> hypotheses;
	
	double propose_p = 0.1; // propose to this many at once 
	
	VectorOfHypotheses(size_t n) : hypotheses(n) { // NOTE default initialized
	}
	
	template<typename... Args>
	VectorOfHypotheses(size_t n, Args&&... args) {
		hypotheses.reserve(n);
		for (size_t i=0; i<n; i++) {
			hypotheses.emplace_back(args...);
		}
	}
	
	void resize(size_t n) {	hypotheses.resize(n);}	
	size_t size() const { return hypotheses.size(); }
	HYP& at(size_t i) { return hypotheses.at(i); }
	const HYP& at(size_t i) const { return hypotheses.at(i); }
	
	[[nodiscard]] virtual std::optional<std::pair<this_t,double>> propose() const override {
		auto should_propose = random_nonempty_subset(hypotheses.size(), propose_p);
		auto out = *this; // copy
		double fb = 0.0;
		for(size_t i=0;i<out.size();i++) {
			// NOTE: here we check can_propose but this may leave us proposing nothing at all -- a slight inefficiency
			if(should_propose.at(i) and hypotheses.at(i).can_propose) {
				auto p = out.at(i).propose();
				if(p){
					auto [h, _fb] = p.value();
					out.at(i) = h;
					fb += _fb;
				}
			}
		}
		return std::make_pair(out, fb);
	}
	
	virtual double compute_prior() override {
		this->prior = 0.0;
		for(auto& h : hypotheses) {
			this->prior += h.compute_prior();
		}
		return this->prior;
	}
	
	[[nodiscard]] virtual this_t restart() const override {
		auto out = *this;
		for(size_t i=0;i<size();i++) {
			out.at(i) = out.at(i).restart();
		}
		return out;
	}
	
	
	virtual bool operator==(const this_t& h) const  override {
		return hypotheses == h.hypotheses; 
	}
	
	virtual size_t hash() const override {
		assert(hypotheses.size() > 0);
		auto hsh = hypotheses.at(0).hash();
		for(size_t i=1;i<hypotheses.size();i++) {
			hash_combine(hsh, hypotheses.at(i).hash() );
		}
		return hsh;
	}
	
	virtual std::string string(std::string prefix="") const override {
		throw NotImplementedError("*** Not implemented for vectors");
	}
	
};