#pragma once 
 
#include "BaseGrammarHypothesis.h"


/**
 * @class DeterministicGrammarHypothesis
 * @author Steven Piantadosi
 * @date 02/05/22
 * @file DeterministicGrammarHypothesis.h
 * @brief Here, the hypotheses are deterministic so we don't need Preidct_t to be a DiscreteDistribution,
 * 		  saving space and making it much faster. 
 */
template<typename this_t, 
         typename _HYP, 
		 typename datum_t=HumanDatum<_HYP>, 
		 typename data_t=std::vector<datum_t>,
		 typename _Predict_t=Vector2D<typename _HYP::output_t>>
class DeterministicGrammarHypothesis : public BaseGrammarHypothesis<this_t, _HYP, datum_t, data_t, _Predict_t> {
public:
	using HYP = _HYP;
	using Super = BaseGrammarHypothesis<this_t, _HYP, datum_t, data_t, _Predict_t>;
	using Super::Super;
	using LL_t = Super::LL_t;
	using Predict_t = Super::Predict_t;
	
	// NOTE: We need to define this constructor or else we call the wrong recompute_LL in the constructor
	// because C++ is a little bit insane. 
	DeterministicGrammarHypothesis(std::vector<HYP>& hypotheses, const data_t* human_data) {
		this->set_hypotheses_and_data(hypotheses, *human_data);
	}	
		
	
	virtual void recompute_P(std::vector<HYP>& hypotheses, const data_t& human_data) override {
		assert(this->which_data == std::addressof(human_data));

		this->P.reset(new Predict_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t h=0;h<hypotheses.size();h++) {			
			
			// sometimes our predicted data is the same as our previous data
			// so we are going to include that so we don't keep recomputing it
			auto ret = hypotheses[h].callOne(*human_data[0].predict);
			
			for(size_t di=0;di<human_data.size();di++) {	
				
				// only change ret if its different
				if(di > 0 and (*human_data[di].predict != *human_data[di-1].predict)) {
					ret = hypotheses[h].callOne(*human_data[di].predict);
				}
				
				#pragma omp critical
//				this->P->at(h,di) = std::move(ret);
				this->P->set(h,di,std::move(ret));
			}
		}
	}
	
	
	virtual std::map<typename HYP::output_t, double> compute_model_predictions(const data_t& human_data, const size_t i, const Matrix& hposterior) const override {
		
		std::map<typename HYP::output_t, double> model_predictions;
		
		for(int h=0;h<hposterior.rows();h++) {
			if(hposterior(h,i) < 1e-6) continue;  // skip very low probability for speed
						
			model_predictions[this->P->get(h,i)] += hposterior(h,i);
		}
		
		return model_predictions;
	}
	

};


 