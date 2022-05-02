#pragma once 
 
#include "GrammarHypothesis.h"


/**
 * @class ThunkGrammarHypothesis
 * @author Steven Piantadosi
 * @date 02/05/22
 * @file ThunkGrammarHypothesis.h
 * @brief A version of a GrammarHypothesis where the hypotheses are thunks (functions of no arguments). In 
 * 		  this case we can get a lot of memory/time savings because we don't have to store predictions for
 * 	 	  each input. 
 */
template<typename this_t, 
         typename _HYP, 
		 typename datum_t=HumanDatum<_HYP>, 
		 typename data_t=std::vector<datum_t>>
class ThunkGrammarHypothesis : public GrammarHypothesis<this_t, _HYP, datum_t, data_t> {
public:
	using HYP = _HYP;
	using Super = GrammarHypothesis<this_t, _HYP, datum_t, data_t>;
	using Super::Super;
	using LL_t = Super::LL_t;
	using Predict_t = Super::Predict_t;
	
	// NOTE: We need to define this constructor or else we call the wrong recompute_LL in the constructor
	// because C++ is a little bit insane. 
	ThunkGrammarHypothesis(std::vector<HYP>& hypotheses, const data_t* human_data) {
		this->set_hypotheses_and_data(hypotheses, *human_data);
	}	
		
	// We are going to override this function because it normally would call the likelihood
	// on EVERY data point, which would be a catastrophe, because compute_likelihood will re-run
	// each program. We want to run each program once, since programs here are stochastic
	// and thunks
	virtual void recompute_LL(std::vector<HYP>& hypotheses, const data_t& human_data) override {
		assert(this->which_data == std::addressof(human_data));
		
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
				const auto M = hypotheses[h].call();
				
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
	
	
	/**
	 * @brief For a thunk, the predictions don't depend on the data
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_P(std::vector<HYP>& hypotheses, const data_t& human_data) override {
		assert(this->which_data == std::addressof(human_data));
		
		this->P.reset(new Predict_t(hypotheses.size(), 1)); 
		
		#pragma omp parallel for
		for(size_t h=0;h<hypotheses.size();h++) {			
			
			// call this with no arguments
			auto ret = hypotheses[h].call();
			
			// we get back a discrete distribution, but we'd like to store it as a vector
			// so its faster to iterate. NOTE: the second element here is exp(x.second), 
			// so we are NOT in log probability anymore
			std::vector<std::pair<typename HYP::output_t,double>> v;
			v.reserve(ret.size());
			
			// TODO: Do we normalize? Here we won't....
			for(const auto& x : ret.values()) {
				v.push_back(std::make_pair(x.first, exp(x.second)));
			}

			#pragma omp critical
			this->P->at(h,0) = std::move(v);
		}
	}
	
	/**
	 * @brief In this variant, we need to always use P->at(h,0) since we only have one prediction stored for thunks
	 * @param i
	 * @param hposterior
	 */	
	virtual std::map<typename HYP::output_t, double> compute_model_predictions(const size_t i, const Matrix& hposterior) const override {
		
		std::map<typename HYP::output_t, double> model_predictions;
		
		for(int h=0;h<hposterior.rows();h++) {
			if(hposterior(h,i) < 1e-6) continue;  // skip very low probability for speed
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			double z = -infinity; 
			for(auto& [s,lp] : this->P->at(h,0)) { // 0 since its a thunk 
				
			}
			
			
			for(const auto& [outcome,outcomeprob] : this->P->at(h,0)) {						
				model_predictions[outcome] += hposterior(h,i) * outcomeprob;
			}
		}
		
		return model_predictions;
	}
	
	
};


 