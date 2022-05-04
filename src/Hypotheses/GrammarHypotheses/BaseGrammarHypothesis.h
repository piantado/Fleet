#pragma once 
 
 // Tune up eigen wrt thread safety
#include <assert.h>
#include <utility>
#include <regex>
#include <signal.h>
#include <unordered_map>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"
#include "DiscreteDistribution.h"

#include "Vector2D.h"
#include "HumanDatum.h"
#include "Control.h"
#include "TopN.h"
#include "MCMCChain.h"

extern volatile sig_atomic_t CTRL_C;

#include "VectorHalfNormalHypothesis.h"
#include "VectorNormalHypothesis.h"
#include "TNormalVariable.h"
#include "Batch.h"

/**
 * @class BaseGrammarHypothesis
 * @author piantado
 * @date 02/08/20
 * @file BaseGrammarHypothesis.h
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
		 typename _Predict_t=Vector2D<DiscreteDistribution<typename _HYP::output_t>> >	// HYP here is the type of the thing we do inference over
class BaseGrammarHypothesis : public MCMCable<this_t, datum_t, data_t>,
						      public Serializable<this_t> {
public:
	using HYP = _HYP;

	// a prediction is a list of pairs of outputs and NON-log probabilities
	// we used to stored this as a map--a DiscreteDistribution--but that was slow to 
	// iterate so now we might make a map in constructing, but we stores as a vector of pairs
	using Predict_t = _Predict_t; 

	// take a data pointer and map it to a hypothesis x i'th item for that data point
	using LL_t = std::unordered_map<typename datum_t::data_t*, std::vector<Vector> >; 
	
	VectorHalfNormalHypothesis logA; 
	
	// Here is a list of built-in parameters that we can use. Each stores a standard
	// normal and a value under the specified transformation, which is chosen here to give 
	// a reasonably shaped prior
//	TNormalVariable< +[](float x)->float { return 1.0/(1.0+expf(-1.7*x)); }> alpha;
	TNormalVariable< +[](float x)->float { return 1.0/(1.0+expf(-1.0*x)); }> alpha;
	TNormalVariable< +[](float x)->float { return expf((x-0.33)/1.50); }>    llt;
//	TNormalVariable< +[](float x)->float { return expf(x/5.0); }>            llt;
	TNormalVariable< +[](float x)->float { return expf(x/5.0); }>            pt;
	TNormalVariable< +[](float x)->float { return expf(x-2); }>              decay;  // peaked near zero
	
	typename HYP::Grammar_t* grammar;
	
	// All of these are shared_ptr so that we can copy hypotheses quickly
	// without recomputing them. NOTE that this assumes that the data does not change
	// between hypotheses of this class. 
	
	std::shared_ptr<Matrix>    C;
	std::shared_ptr<LL_t>      LL; // type for the likelihood
	std::shared_ptr<Predict_t> P;

	// These variables store some parameters and get recomputed
	// in proposal when necessary 
	std::shared_ptr<Matrix> decayedLikelihood;
	
	// stored so we can remember what we computed for. 
	const data_t* which_data; 
	const std::vector<HYP>* which_hypotheses;

	BaseGrammarHypothesis() {	}
		
	BaseGrammarHypothesis(std::vector<HYP>& hypotheses, const data_t* human_data) {
		// This has to take human_data as a pointer because of how MCMCable::make works -- can't forward a reference
		// but the rest of this class likes the references, so we convert here
		this->set_hypotheses_and_data(hypotheses, *human_data);		
	}	
	
	// we overwrite this because in MCMCable, this wants to check get_value, which is not defined here
	[[nodiscard]] static this_t sample(std::vector<HYP>& hypotheses, const data_t* human_data) {
		// NOTE: Cannot use templates because then it doesn't pass my hypotheses ref the right way
		auto h = this_t(hypotheses, human_data);
		return h.restart();
	}

	/**
	 * @brief This is the primary function for setting hypothese and data on construction. 
	 * @param hypotheses
	 * @param human_data
	 */	
	virtual void set_hypotheses_and_data(std::vector<HYP>& hypotheses, const data_t& human_data) {
		
		// set first because it's required below
		which_data = std::addressof(human_data);
		which_hypotheses = std::addressof(hypotheses);
		
		// read the hypothesis from the first grammar, and check its the same for everyone	
		grammar = hypotheses.at(0).get_grammar();		
		for(auto& h: hypotheses) {
			assert(h.get_grammar() == grammar && "*** It's bad news for GrammarHypothesis if your hypotheses don't all have the same grammar.");
		}
		
		logA.set_size(grammar->count_rules());
		
		// when we are initialized this way, we compute C, LL, P, and the decayed ll. 
		this->recompute_C(hypotheses);
		COUT "# Done computing prior counts" ENDL;
		this->recompute_P(hypotheses, human_data); // note this comes before LL because LL might use P
		COUT "# Done computing model predictions" ENDL;
		this->recompute_LL(hypotheses, human_data);
		COUT "# Done computing incremental likelihoods " ENDL;

		// this gets constructed here so it doesn't need to be reallocated each time we call recompute_decayedLikelihood
		this->decayedLikelihood.reset(new Matrix(C->rows(), human_data.size()));
		this->recompute_decayedLikelihood(human_data);
		COUT "# Done computing decayedLikelihood" ENDL;
		COUT "# Starting MCMC" ENDL;
	}
		
	/**
	 * @brief Set whether I can propose to a value in logA -- this is handled by VectorNormalHypothesis
	 * 		  Here, though, we warn if the value is not 1.0
	 * @param i
	 * @param b
	 */	
	virtual void set_can_propose(size_t i, bool b) {
		logA.set_can_propose(i,b);
		
		if(logA(i) != 0.0) { CERR "# Warning, set_can_propose is setting false to logA(" << str(i) << ") != 0.0 (this is untransformed space)" ENDL; }
	}
	
	/**
	 * @brief A convenient function that uses C to say how many hypotheses
	 * @return 
	 */	
	virtual size_t nhypotheses() const { return C->rows(); }
	
	/**
	 * @brief Computes our matrix C[h,r] of hypotheses (rows) by counts of each grammar rule.
	 *			(requires that each hypothesis use the same grammar)
	 * @param hypotheses
	 */
	virtual void recompute_C(std::vector<HYP>& hypotheses) {
		   
		assert(hypotheses.size() > 0);

		size_t nRules = hypotheses[0].get_grammar()->count_rules();

		C.reset(new Matrix(hypotheses.size(), nRules)); 

		#pragma omp parallel for
		for(size_t i=0;i<hypotheses.size();i++) {
			auto c = hypotheses[i].get_grammar()->get_counts(hypotheses[i].get_value());
			Vector cv = Vector::Zero(c.size());
			for(size_t k=0;k<c.size();k++){
				cv(k) = c[k];
			}
			
			assert(hypotheses[i].get_grammar() == grammar); // just a check that the grammars are identical
			
			#pragma omp critical
			C->row(i) = std::move(cv);
		}
		
		assert( (size_t)C->rows() == hypotheses.size());
	}
		
	/**
	 * @brief Recompute LL[h,di] a hypothesis from each hypothesis and data point to a *vector* of prior responses.
	 * 		  (We need the vector instead of just the sum to implement memory decay
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_LL(std::vector<HYP>& hypotheses, const data_t& human_data) {
		assert(which_data == std::addressof(human_data));
			
		// For each HumanDatum::data, figure out the max amount of data it contains
		std::unordered_map<typename datum_t::data_t*, size_t> max_sizes;
		for(auto& d : human_data) {
			if( (not max_sizes.contains(d.data)) or max_sizes[d.data] < d.ndata) {
				max_sizes[d.data] = d.ndata;
			}
		}
	
		LL.reset(new LL_t()); 
		LL->reserve(max_sizes.size()); // reserve for the same number of elements 
		
		// now go through and compute the likelihood of each hypothesis on each data set
		for(const auto& [dptr, sz] : max_sizes) {
			if(CTRL_C) break;
				
			LL->emplace(dptr, nhypotheses()); // in this place, make something of size nhypotheses
			
			#pragma omp parallel for
			for(size_t h=0;h<nhypotheses();h++) {
				
				// set up all the likelihoods here
				Vector data_lls  = Vector::Zero(sz);				
				
				// read the max size from above and compute all the likelihoods
				for(size_t i=0;i<max_sizes[dptr];i++) {
					
					// here we could do 
					// data_lls(i) = hypotheses[h].compute_single_likelihood(x.first->at(i));
					// but the problem is that isn't defined for some hypotheses. So we'll use 
					// the slower
					typename HYP::data_t d;
					d.push_back(dptr->at(i));
					data_lls(i) = hypotheses[h].compute_likelihood(d);
										
					assert(not std::isnan(data_lls(i))); // NaNs will really mess everything up
				}
				
				#pragma omp critical
				LL->at(dptr)[h] = std::move(data_lls);
			}
		}
		
	}
	
	/**
	 * @brief Recomputes the decayed likelihood (e.g. at the given decay level, the total ll for each data point
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_decayedLikelihood(const data_t& human_data) {
		assert(which_data == std::addressof(human_data));
		
		// find the max power we'll ever need
		int MX = -1;
		for(auto& di : human_data) {
			for(auto& dp : *di.decay_position) {
				MX = std::max(MX, dp+1); // need +2 since 0 decay needs one value
			}
		}
		
		// just compute this once -- should be faster to use vector intrinsics? 
		// we store these in reverse order from some max data size
		// so that we can just take the tail for what we need
		Vector powers = Vector::Ones(MX);
		for(int i=1;i<MX;i++) { // intentionally leaving powers(0) = 1 here
			powers(i) = powf(i,-decay.get());
		}
		
		// fix it if its the wrong size
		if(decayedLikelihood->rows() != C->rows() or
		   decayedLikelihood->cols() != (int)human_data.size()) {
			decayedLikelihood.reset(new Matrix(nhypotheses(), human_data.size()));
	    } // else we don't need to do anything since it' all overwritten below
		   
		// sum up with the memory decay
		#pragma omp parallel for
		for(size_t h=0;h<nhypotheses();h++) {
			for(int di=0;di<(int)human_data.size();di++) {
				const datum_t& d = human_data[di];
				const Vector& v = LL->at(d.data)[h]; // get the pre-computed vector of data here
				
				// what is the decay position of the thing we are using?Rs
				const int K = d.my_decay_position;
			
				double dl = 0.0; // the decayed likelihood value here
				for(size_t k=0;k<d.ndata;k++) {
					dl += v(k) * powers(K - d.decay_position->at(k)); // TODO: This could be stored as a vector -- might be faster w/o loop?
				}

				// #pragma may not be needed?
				#pragma omp critical
				assert(not std::isnan(dl));
				(*decayedLikelihood)(h,di) = dl;							
			}
		}
	}
	
	/**
	 * @brief Recompute the predictions for the hypotheses and data
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_P(std::vector<HYP>& hypotheses, const data_t& human_data) = 0;
	
		/**
	 * @brief This uses hposterior (computed via this->compute_normalized_posterior()) to compute the model predictions
	 * 		  on a the i'th human data point. To do this, we marginalize over hypotheses, computing the weighted sum of
	 *        outputs. 
	 * @param hd
	 * @param hposterior
	 */
	virtual std::map<typename HYP::output_t, double> compute_model_predictions(const data_t& human_data, const size_t i, const Matrix& hposterior) const = 0;
	
	
	/**
	 * @brief Get the chance probability of response r in hd (i.e. of a human response). This may typically be pretty boring (like just hd.chance) but w
	 * 	      we need to be able to overwrite it
	 * @param hd
	 * @return 
	 */	
	virtual double human_chance_lp(const typename datum_t::output_t& r, const datum_t& hd) const {
		return log(hd.chance);
	}
	
	
	virtual double compute_prior() override {
		return this->prior = logA.compute_prior() + 
							 alpha.compute_prior() +
							 llt.compute_prior() + 
							 pt.compute_prior() + 
							 decay.compute_prior();
	}
	
	/**
	 * @brief This returns a matrix hposterior[h,di] giving the posterior on the h'th element. NOTE: not output is NOT logged
	 * @return 
	 * @return 
	 */	
	virtual Matrix compute_normalized_posterior() const {
		
		// the model's posterior
		// do we need to normalize the prior here? The answer is no -- because its just a constant
		// and that will get normalized away in posterior
		const auto hprior =  this->hypothesis_prior(*C);
		const auto hlikelihood = (*decayedLikelihood / llt.get());
		Matrix hposterior = hlikelihood.colwise() + hprior;
		//Matrix hposterior = (*decayedLikelihood).colwise() + hprior;
		
		// now normalize it and convert to probabilities
		#pragma omp parallel for
		for(int di=0;di<hposterior.cols();di++) { 
			
			// here we normalize and convert it to *probability* space
			const Vector& v = hposterior.col(di); 
			const Vector lv = lognormalize(v).array().exp();
			// wow it's a real mystery that the below does not work
			// (it compiles, but gives weirdo answers...)
			// const auto& lv = lognormalize(hposterior.col(di)).array().exp();
			
			#pragma omp critical
			hposterior.col(di) = lv;
		}
		
		return hposterior;
	}
	
	/**
	 * @brief This computes the likelihood of the *human data*.
	 * @param data
	 * @param breakout
	 * @return 
	 */		
	virtual double compute_likelihood(const data_t& human_data, const double breakout=-infinity) override {
		
		// recompute our likelihood if its the wrong size or not using this data
		if(which_data != std::addressof(human_data)) { 
			CERR "*** You are computing likelihood on a dataset that the BaseGrammarHypothesis was not constructed with." ENDL
			CERR "		You must call set_hypotheses_and_data before calling this likelihood, but note that when you" ENDL
			CERR "      do that, it will need to recompute everything (which might take a while)." ENDL;
			assert(false);
		}		


		// Ok this needs a little explanation. If we have overwritten the types, then we can get compilation
		// errors below because for instance r.first won't be of the right type to index into model_predictions.
		// So this line catches that and removes this chunk of code at compile time if we have removed
		// those types. In its place, we add a runtime assertion fail, meaning you should have overwritten
		// compute_likelihood if you change these types
		if constexpr(std::is_same<std::vector<std::pair<typename HYP::output_t,size_t>>, typename datum_t::response_t>::value) {
			
			Matrix hposterior = this->compute_normalized_posterior();
			
			this->likelihood  = 0.0; // for all the human data
			
			#pragma omp parallel for
			for(size_t i=0;i<human_data.size();i++) {
				
				auto model_predictions = compute_model_predictions(human_data, i, hposterior);			
				
				double ll = 0.0; // the likelihood here
				auto& di = human_data[i];
				for(const auto& [r,cnt] : di.responses) {
					ll += cnt * logplusexp( log(1-alpha.get()) + human_chance_lp(r,di), 
											log(alpha.get()) + log(model_predictions[r])); 
				}
				
				
				#pragma omp atomic
				this->likelihood += ll;
			}
			
			return this->likelihood;	
		}
		else {
			assert(false && "*** If you use BaseGrammarHypothesis with non-default types, you need to overwrite compute_likelihood so it does the right thing.");
		}
	
	}
	
	[[nodiscard]] virtual this_t restart() const override {
		this_t out(*static_cast<const this_t*>(this)); // copy
		
		out.logA = logA.restart();
		
		out.alpha = alpha.restart();
		out.decay = decay.restart();
		out.llt   = llt.restart();
		out.pt    = pt.restart();
		
		out.recompute_decayedLikelihood(*out.which_data);
		
		return out;
	}
	
	 /**
	 * @brief Propose to the hypothesis. The sometimes does grammar parameters and sometimes other parameters, 
	 * 		  which are all stored as VectorHypotheses. It is sure to call the recompute functions when necessary
	 * @param data
	 * @param breakout
	 * @return 
	 */		
	[[nodiscard]] virtual std::optional<std::pair<this_t,double>> propose() const override {
		
		// make a copy
		this_t out(*static_cast<const this_t*>(this)); 
		
		if(flip(0.15)){
			
			// see what to propose to
			double myfb = 0.0;
			switch(myrandom(4)) {
				case 0: {
					auto p = alpha.propose();
					if(not p) return {};
					auto [ v, fb ] = p.value();
					out.alpha = v;
					myfb += fb;
					break;
				}
				case 1: {
					auto p = llt.propose();
					if(not p) return {};
					auto [ v, fb ] = p.value();
					out.llt = v;
					myfb += fb;
					break;
				}
				case 2: {
					auto p = pt.propose();
					if(not p) return {};
					auto [ v, fb ] = p.value();
					out.pt = v;
					myfb += fb;
					break;
				}
				case 3: {
					auto p = decay.propose();
					if(not p) return {};
					auto [ v, fb ] = p.value();
					out.decay = v;
					myfb += fb;
					break;
				}
				default: assert(false);
			}
			
			// if we need to recompute the decay:
			if(decay != out.decay)	{
				out.recompute_decayedLikelihood(*out.which_data); // must recompute this when we get to likelihood
			}			
			return std::make_pair(out, myfb);
		}		
		else {
			//CERR "HERE" ENDL;
			auto p = logA.propose();
			if(not p) return {};
			auto [ v, fb ] = p.value();
			out.logA = v;
			return std::make_pair(out, fb);
		}
	}
	
	/**
	 * @brief Compute a vector of the prior (one for each hypothesis) using the given counts matrix (hypotheses x rules), AT the specified
	 * 	      temperature
	 * @param C
	 * @return 
	 */	
	virtual Vector hypothesis_prior(const Matrix& myC) const {
		return lognormalize( (*C) * (-logA.value) / pt.get() );			
		
		// This version does the rational-rule thing and requires that logA store the log of the A values
		// that ends up giving kinda crazy values. NOTE: This version also constrains each NT to sum to 1,
		// which our general version below does not. 

		//		Vector out(nhypotheses()); // one for each hypothesis

		// get the marginal probability of the counts via  dirichlet-multinomial
		//Vector allA = logA.value.array().exp(); // translate [-inf,inf] into [0,inf]
		
//		#pragma omp parallel for
//		for(auto i=0;i<myC.rows();i++) {
//			
//			double lp = 0.0;
//			size_t offset = 0; // 
//			for(size_t nt=0;nt<grammar->count_nonterminals();nt++) { // each nonterminal in the grammar is a DM
//				size_t nrules = grammar->count_rules( (nonterminal_t) nt);
//				if(nrules > 1) { // don't need to do anything if only one rule (but must incremnet offset)
//					Vector a = eigenslice(allA,offset,nrules); // TODO: seqN doesn't seem to work with this eigen version
//					Vector c = eigenslice(myC.row(i),offset,nrules);
//					
//					// This version does RR marginalization and requires us to use logA.value.array().exp() above. 
//					double n = a.sum(); assert(n > 0.0); 
//					double c0 = c.sum();
//					if(c0 != 0.0) { // TODO: Check this...  
//						// NOTE: std::lgamma here is not thread safe, so we use mylgamma defined in Numerics
//						lp += mylgamma(n+1) + mylgamma(c0) - mylgamma(n+c0) 
//							 + (c.array() + a.array()).array().lgamma().sum() 
//							 - (c.array()+1).array().lgamma().sum() 
//							 - a.array().lgamma().sum();
//					}
//				}
//				offset += nrules;
//			}
//		
//			#pragma omp critical
//			out(i) = lp / temp; // NOTE: we use the temp before we lognormalize
//		}		
	
	}
	
	virtual bool operator==(const this_t& h) const override {
		return (C == h.C and LL == h.LL and P == h.P) and 
				(logA == h.logA)   and 
				(alpha == h.alpha) and
				(llt   == h.llt)   and
				(pt    == h.pt)    and
				(decay == h.decay);
	}
	
	/**
	 * @brief This returns a string for this hypothesis. Defaulty, now, just in tidy format
	 * 		  with all the parameters, one on each row. Note these parameters are shown after
	 * 		  transformation (e.g. the prior parameters are NOT logged)
	 * @return 
	 */	
	virtual std::string string(std::string prefix="") const override {
		// For now we just provide partial information
		//return "GrammarHypothesis["+str(this->posterior) + params.string() + ", " + logA.string() + "]";
		std::string out = ""; 
		
		// the -1 here is just to contrast with the nt printed below
		out += prefix + "-1\tposterior.score" +"\t"+ str(this->posterior) + "\n";
		out += prefix + "-1\tparameter.prior" +"\t"+ str(this->prior) + "\n";
		out += prefix + "-1\thuman.likelihood" +"\t"+ str(this->likelihood) + "\n";
		out += prefix + "-1\talpha" +"\t"+ str(alpha.get()) + "\n";
		out += prefix + "-1\tllt" +"\t"+ str(llt.get()) + "\n";
		out += prefix + "-1\tpt" +"\t"+ str(pt.get()) + "\n";
		out += prefix + "-1\tdecay" +"\t"+ str(decay.get()) + "\n";
		
		// now add the grammar operations
		size_t xi=0;
		for(auto& r : *grammar) {
			if(logA.can_propose[xi]) { // we'll skip the things we set as effectively constants (but still increment xi)
				std::string rs = r.format;
				rs = std::regex_replace(rs, std::regex("\\%s"), "X");
				out += prefix + str(r.nt) + "\t" + QQ(rs) +"\t" + str(logA(xi)) + "\n"; // unlogged here
			}
			xi++;
		}	
		out.erase(out.size()-1,1); // delete the final newline 
		
		return out;	
	}
	
	/**
	 * @brief Need to override print since it will print in a different format
	 * @return 
	 */	
	virtual void print(std::string prefix="") override {
		COUT string(prefix) ENDL;
	}
	
	virtual size_t hash() const override { 
		size_t output = logA.hash();
		hash_combine(output, alpha.hash(), decay.hash(), llt.hash(),  pt.hash());
		return output;
	}
	
	virtual std::string serialize() const override { throw NotImplementedError();}
	static this_t deserialize(const std::string& s) { throw NotImplementedError(); }
	
};