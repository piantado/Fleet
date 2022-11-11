#pragma once 
 
#include "BaseGrammarHypothesis.h"

/**
 * @class FullGrammarHypothesis
 * @author piantado
 * @date 02/08/20
 * @file FullGrammarHypothesis.h
 * @brief This class does grammar inference with some collection of HumanData and fixed set of hypotheses. 
 * 
 * 			NOTE: This is set up in way that it recomputes variables only when necessary, allowing it to make copies (mostly of pointers)
 * 				  to components needed for the likelihood. This means, proposals maintain pointers to old values of Eigen matrices
 * 				  and these are tracked using shared_ptr 
 * 			NOTE: Without likelihood decay, this would be much faster. It should work fine if likelihood decay is just set once. 
 * 
 */
template<typename this_t, 
         typename _HYP, 
		 typename datum_t=HumanDatum<_HYP>, 
		 typename data_t=std::vector<datum_t>,
		 typename _Predict_t=Vector2D<typename _HYP::output_t>>
class FullGrammarHypothesis : public BaseGrammarHypothesis<this_t, _HYP, datum_t, data_t, _Predict_t> {
public:
	using HYP = _HYP;
	using Super = BaseGrammarHypothesis<this_t, _HYP, datum_t, data_t, _Predict_t>;
	using Super::Super;
	using LL_t = Super::LL_t;
	using Predict_t = Super::Predict_t;

	/**
	 * @brief Recompute the predictions for the hypotheses and data
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_P(std::vector<HYP>& hypotheses, const data_t& human_data) {
		assert(which_data == std::addressof(human_data));

		P.reset(new Predict_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t h=0;h<hypotheses.size();h++) {			
			
			// sometimes our predicted data is the same as our previous data
			// so we are going to include that so we don't keep recomputing it
			auto ret = hypotheses[h].call(*human_data[0].predict);
			
			for(size_t di=0;di<human_data.size();di++) {	
				
				// only change ret if its different
				if(di > 0 and (*human_data[di].predict != *human_data[di-1].predict)) {
					ret = hypotheses[h].call(*human_data[di].predict);
				}
				
				#pragma omp critical
				P->at(h,di) = ret; // cannot move because ret might be reused
			}
		}
	}

	
	/**
	 * @brief This uses hposterior (computed via this->compute_normalized_posterior()) to compute the model predictions
	 * 		  on a the i'th human data point. To do this, we marginalize over hypotheses, computing the weighted sum of
	 *        outputs. 
	 * @param hd
	 * @param hposterior
	 */
	virtual std::map<typename HYP::output_t, double> compute_model_predictions(const data_t& human_data, const size_t i, const Matrix& hposterior) const {
	
		std::map<typename HYP::output_t, double> model_predictions;
		
		// Note this is the non-log version. Here we avoid computing ang logs, logsumexp, or even exp in this loop
		// because it is very slow. 
		// P(output | learner_data) = sum_h P(h | learner_data) * P(output | h):
		// NOTE We do not use omp because this is called by something that does
		for(int h=0;h<hposterior.rows();h++) {
			if(hposterior(h,i) < 1e-6) continue;  // skip very low probability for speed
			
			for(const auto& [outcome,outcomelp] : P->at(h,i)) {						
				model_predictions[outcome] += hposterior(h,i) * exp(outcomelp);
			}
		}
		
		return model_predictions;
	}
	
};
