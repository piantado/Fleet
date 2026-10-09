#pragma once

#include "HumanDatum.h"

// This is a HumanDatum plus some sutff for dealing with hypotheses
// ConditionOn_t can either by std::vector of data or std::span, depending on where we want to store the conditioning data
template<typename HYP, typename _ConditionOn_t=HYP::data_t>
struct HumanDataBundle : public HumanDatum<HYP,_ConditionOn_t> {
	using ConditionOn_t = _ConditionOn_t;
	using Hinput_t = typename HYP::input_t;
	
	// To HumanDatum we are going to add responses and caching etc
	// from each hypothesis
	
	// each hypothesis output on generalize
	std::vector<typename HYP::output_t> hypotheses_responses; 
	
	// each hypothesis's likelihood to each conditioned_on
	std::vector<std::vector<float>> point_likelihood;  
	
	HumanDataBundle(std::vector<HYP>& hypotheses, 
					HumanDatum<HYP,ConditionOn_t>& d) :
		HumanDatum<HYP,ConditionOn_t>(d) // copy over everything from d
	{
		fill_point_likelihoods(hypotheses);
		fill_predictions(hypotheses);
	}

	HumanDataBundle(std::vector<HYP>& hypotheses, 
					ConditionOn_t cond, 
					Hinput_t gen, 
					std::map<typename HYP::output_t,int> resp,
					std::vector<float> decays) : 
			HumanDatum<HYP, ConditionOn_t>(cond, decays, gen, resp)
	{
		const size_t N = this->condition_on.size();
		
		assert(this->decay_indices.size() == N); 
		
		fill_point_likelihoods(hypotheses);
		fill_predictions(hypotheses);
	}
	
	void fill_point_likelihoods(std::vector<HYP>& hypotheses) {
		const size_t H = hypotheses.size();
		
		// fill in the point likelihoods
		point_likelihood.resize(H); 
		for(size_t hi=0;hi<H;hi++) {
			auto& h = hypotheses.at(hi);
			point_likelihood.at(hi) = std::vector<float>(this->condition_on.size(), 0.0);
			for(size_t di=0;di<this->condition_on.size();di++) {
				point_likelihood.at(hi).at(di) = h.compute_single_likelihood(this->condition_on[di]);
			}
		}
	}
	
	void fill_predictions(std::vector<HYP>& hypotheses) {
		const size_t H = hypotheses.size();
		
		// fill in the predictions of each hypothesis
		hypotheses_responses.resize(H);
		for(size_t hi=0;hi<H;hi++) {
			auto& h = hypotheses.at(hi);
			hypotheses_responses.at(hi) = h.call(this->generalize);
		}
	}
	
	// what is the hi'th liklihood if it has decay decay?
	double likelihood_with_decay(size_t hi, float decay) {
		double ll = 0.0;
		for(size_t i=0;i<this->condition_on.size();i++) {
			ll += point_likelihood.at(hi).at(i) * pow( this->decay_indices.at(i), -decay);
		}
		return ll;
	}
};
