#pragma once 

#include "ThunkGrammarHypothesis.h"

class MyGrammarHypothesis final : public ThunkGrammarHypothesis<MyGrammarHypothesis, 
																MyHypothesis, 
																MyHumanDatum,
																std::vector<MyHumanDatum>,
																Vector2D<DiscreteDistribution<S>>> {
public:
	using Super = ThunkGrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum, std::vector<MyHumanDatum>, Vector2D<DiscreteDistribution<S>>>;
	using Super::Super;
	using data_t = Super::data_t;


	// remove any strings in human_data[0...n] from M, and then renormalize 
	static void remove_strings_and_renormalize(DiscreteDistribution<S>& M, datum_t::data_t* dptr, const size_t n) {
		// now we need to renormalize for the fact that we can't produce strings in the 
		// observed set of data (NOTE This assumes that the data can't be noisy versions of
		// strings that were in teh set too). 
		for(size_t i=0;i<n;i++) {
			const auto& s = dptr->at(i).output;
			if(M.contains(s)) {
				M.erase(s);
			}
		}
		
		// and renormalize M with the strings removed
		double Z = M.Z();
		for(auto [s,lp] : M){
			M.m[s] = lp-Z;
		}
		
	}


	virtual double human_chance_lp(const typename datum_t::output_t& r, const datum_t& hd) const override {
		// here we are going to make chance be exponential in the length of the response
		return -(double)(r.length()+1)*log(alphabet.size()+1); // NOTE: Without the double case, we negate r.length() first and it's awful
	}	
	
	virtual void recompute_LL(std::vector<HYP>& hypotheses, const data_t& human_data) override {
		assert(this->which_data == std::addressof(human_data));
		
		// we need to define a version of LL where we DO NOT include the observed strings in the model-based
		// likelihood, since people aren't allowed to respond with them. 
		
		
		// For each HumanDatum::data, figure out the max amount of data it contains
		std::unordered_map<typename datum_t::data_t*, size_t> max_sizes;
		for(auto& d : human_data) {
			if( (not max_sizes.contains(d.data)) or max_sizes[d.data] < d.ndata) {
				max_sizes[d.data] = d.ndata;
			}
		}
	
		this->LL.reset(new LL_t()); 
		this->LL->reserve(max_sizes.size()); // reserve for the same number of elements 
		
		// now go through and compute the likelihood of each hypothesis on each data set
		for(const auto& [dptr, sz] : max_sizes) {
			if(CTRL_C) break;
				
			this->LL->emplace(dptr, this->nhypotheses()); // in this place, make something of size nhypotheses
			
			#pragma omp parallel for
			for(size_t h=0;h<this->nhypotheses();h++) {
				
				// set up all the likelihoods here
				Vector data_lls  = Vector::Zero(sz);				
				
				// call this just onece and then use it for all the string likelihoods
				auto M = P->at(h,0); // can just copy 
				//!!
				remove_strings_and_renormalize(M, dptr, max_sizes[dptr]);
				//!!
				
				// read the max size from above and compute all the likelihoods
				for(size_t i=0;i<max_sizes[dptr];i++) {
					typename HYP::data_t d;
					d.push_back(dptr->at(i));
					
					data_lls(i) = MyHypothesis::string_likelihood(M, d);
					
					assert(not std::isnan(data_lls(i))); // NaNs will really mess everything up
				}
				
				#pragma omp critical
				this->LL->at(dptr)[h] = std::move(data_lls);
			}
		}
		
	}
	

	virtual std::map<typename HYP::output_t, double> compute_model_predictions(const data_t& human_data, const size_t i, const Matrix& hposterior) const override {
		
		// NOTE: we must also define a version of this which renormalizes  by the data
		// we could do this in P except then P would have to be huge for thunks since P
		// would depend on the data. So we will do it here. 
	
		std::map<typename HYP::output_t, double> model_predictions;
		
		for(int h=0;h<hposterior.rows();h++) {
			if(hposterior(h,i) < 1e-6) continue;  // skip very low probability for speed
			
			auto M = P->at(h,0);
			//!!
			remove_strings_and_renormalize(M, human_data[i].data, human_data[i].ndata);
			//!!
			
			for(const auto& [outcome,outlp] : M) {						
				model_predictions[outcome] += hposterior(h,i) * exp(outlp);
			}
		}
		
		return model_predictions;
	}
	
	
	
};