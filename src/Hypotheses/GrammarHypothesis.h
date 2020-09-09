#pragma once 
 
 // Tune up eigen wrt thread safety
#include <assert.h>
#include <regex>
#include <signal.h>

#include "EigenLib.h"
#include "Errors.h"
#include "Numerics.h"
#include "DiscreteDistribution.h"

#include "Vector2D.h"
#include "HumanDatum.h"
#include "Control.h"
#include "Top.h"
#include "MCMCChain.h"

extern volatile sig_atomic_t CTRL_C;

#include "VectorHypothesis.h"

// If we do this, then we compute the grammar hypothesis's predictive values in log space. This is more
// accurate but quite a bit slower, so it's off by default. 
//#define GRAMMAR_HYPOTHESIS_USE_LOG_PREDICTIVE 1

/**
 * @brief Runs MCMC on hypotheses, resampling when the data stops being incremental and returns a unioned
 * 			vector of all the tops
 * @param hypotheses
 * @return 
 */
template<typename HYP>
std::vector<HYP> get_hypotheses_from_mcmc(HYP& h0, std::vector<typename HYP::data_t*>& mcmc_data, Control c, size_t ntop) {
	
	std::set<HYP> all;	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<mcmc_data.size();vi++) {
		if(!CTRL_C) {  // needed for openmp
		
			#pragma omp critical
			{
			COUT "# Running " TAB vi TAB " of " TAB mcmc_data.size() ENDL;
			}
			
			for(size_t i=1;i<=mcmc_data[vi]->size() and !CTRL_C;i++) {
				
				TopN<HYP> top(ntop);
				
				HYP myh0 = h0.restart();
				
				// slices [0,i]
				auto givendata = slice(*(mcmc_data[vi]), 0, i);
				
				MCMCChain chain(myh0, &givendata, top);
				chain.run(Control(c)); // must run on a copy 
			
				#pragma omp critical
				for(auto h : top.values()) {
					h.clear_bayes(); // zero and insert
					all.insert(h);
				}
			}
		}
	}
	
	std::vector<HYP> out;
	for(auto& h : all) {
		out.push_back(h);
	}
	return out;
}





/**
 * @class GrammarHypothesis
 * @author piantado
 * @date 02/08/20
 * @file GrammarHypothesis.h
 * @brief This class does grammar inference with some collection of HumanData and fixed set of hypotheses. 
 * 
 * 			NOTE: This is set up in way that it recomputes variables only when necessary, allowing it to make copies (mostly of pointers)
 * 				  to components needed for the likelihood. This means, proposals maintain pointers to old values of Eigen matrices
 * 				  and these are tracked using shared_ptr 
 * 			NOTE: Without likelihood decay, this would be much faster. It should work fine if likelihood decay is just set once. 
 * 
 */
template<typename this_t, typename HYP, typename datum_t=HumanDatum<HYP>, typename data_t=std::vector<datum_t>>	// HYP here is the type of the thing we do inference over
class GrammarHypothesis : public MCMCable<this_t, datum_t, data_t>  {
public:

	// index by hypothesis, data point, an Eigen Vector of all individual data point likelihoods
	typedef Vector2D<std::pair<Vector,Vector>> LL_t; // likelihood type

	// a prediction is a list of pairs of outputs and NON-log probabilities
	// we used to stored this as a map--a DiscreteDistribution--but that was slow to 
	// iterate so now we might make a map in constructing, but we stores as a vector of pairs
	typedef Vector2D<std::vector<std::pair<typename HYP::output_t,double>>> Predict_t; 
		
	VectorHypothesis  logA; // a simple normal vector for the log of a
	
	// This stores all of our parameters -- with standard normal priors
	VectorHypothesis params; // alpha, likelihood temperature, decay
	constexpr static int NPARAMS = 4; // how many parameters do we have?
	
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
	double alpha, llt, decay, pt; // the parameters 
	
	// stored so we can remember what we computed for. 
	const data_t* which_data; 
			
	GrammarHypothesis() {	}
	
	GrammarHypothesis(std::vector<HYP>& hypotheses, const data_t* human_data) {
		// This has to take human_data as a pointer because of how MCMCable::make works -- can't forward a reference
		// but the rest of this class likes the references, so we convert here
		set_hypotheses_and_data(hypotheses, *human_data);
	}	
	
	/**
	 * @brief This is the primary function for setting hypothese and data on construction. 
	 * @param hypotheses
	 * @param human_data
	 */	
	virtual void set_hypotheses_and_data(std::vector<HYP>& hypotheses, const data_t& human_data) {
		
		// set first because it's required below
		which_data = std::addressof(human_data);
		
		// read the hypothesis from the first grammar, and check its the same for everyone	
		grammar = hypotheses.at(0).grammar;		
		for(auto& h: hypotheses) {
			assert(h.grammar == grammar && "*** It's bad news for GrammarHypothesis if your hypotheses don't all have the same grammar.");
		}
		
		logA.set_size(grammar->count_rules());
		params.set_size(NPARAMS); 
		
		// when we are initialized this way, we compute C, LL, P, and the decayed ll. 
		recompute_C(hypotheses);
		COUT "# Done computing prior counts" ENDL;
		recompute_LL(hypotheses, human_data);
		COUT "# Done computing incremental likelihoods " ENDL;
		recompute_P(hypotheses, human_data);
		COUT "# Done computing model predictions" ENDL;
		// this gets constructed here so it doesn't need to be reallocated each time we call recompute_decayedLikelihood
		decayedLikelihood.reset(new Matrix(C->rows(), human_data.size()));
		recompute_decayedLikelihood(human_data);
		COUT "# Done computing decayedLikelihood" ENDL;
		COUT "# Starting MCMC" ENDL;
	}
	
	virtual void recompute_alpha() { alpha = 1.0/(1.0+expf(-3.0*params(0))); }
	virtual void recompute_llt()   { llt = expf(params(1)/5.0);	} // centered near 1
	virtual void recompute_decay() { decay = expf(params(2)-2); } // peaked near zero
	virtual void recompute_pt()    { pt  = expf(params(3)/5.0); }
	
	/**
	 * @brief Set whether I can propose to a value in logA -- this is handled by VectorHypothesis
	 * 		  Here, though, we warn if the value is not 1.0
	 * @param i
	 * @param b
	 */
	
	virtual void set_can_propose(size_t i, bool b) {
		logA.set_can_propose(i,b);
		
		if(logA(i) != 0.0) {
			CERR "# Warning, set_can_propose is setting false to logA(" << str(i) << ") != 0.0 (this is untransformed space)" ENDL;
		}
		
	}
	
	/**
	 * @brief A convenient function that uses C to say how many hypotheses
	 * @return 
	 */	
	virtual size_t nhypotheses() const {
		return C->rows();
	}
	
	/**
	 * @brief Computes our matrix C[h,r] of hypotheses (rows) by counts of each grammar rule.
	 *			(requires that each hypothesis use the same grammar)
	 * @param hypotheses
	 */
	virtual void recompute_C(std::vector<HYP>& hypotheses) {
		   
		assert(hypotheses.size() > 0);

		size_t nRules = hypotheses[0].grammar->count_rules();

		C.reset(new Matrix(hypotheses.size(), nRules)); 

		#pragma omp parallel for
		for(size_t i=0;i<hypotheses.size();i++) {
			auto c = hypotheses[i].grammar->get_counts(hypotheses[i].get_value());
			Vector cv = Vector::Zero(c.size());
			for(size_t k=0;k<c.size();k++){
				cv(k) = c[k];
			}
			
			assert(hypotheses[i].grammar == grammar); // just a check that the grammars are identical
			
			#pragma omp critical
			C->row(i) = cv;
		}
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
		
		LL.reset(new LL_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t h=0;h<hypotheses.size();h++) {
			
			if(!CTRL_C) {
				
				// This will assume that as things are sorted in human_data, they will tend to use
				// the same human_data.data pointer (e.g. they are sorted this way) so that
				// we can re-use prior likelihood items for them
				
				for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
					
					Vector data_lls  = Vector::Zero(human_data[di].ndata); // one for each of the data points
					Vector decay_pos = Vector::Zero(human_data[di].ndata); // one for each of the data points
					
					// check if these pointers are equal so we can reuse the previous data			
					if(di > 0 and 
					   human_data[di].data == human_data[di-1].data and 
					   human_data[di].ndata > human_data[di-1].ndata) { 
						   
						// just copy over the beginning
						for(size_t i=0;i<human_data[di-1].ndata;i++){
							data_lls(i)  = LL->at(h,di-1).first(i);
							decay_pos(i) = LL->at(h,di-1).second(i);
						}
						// and fill in the rest
						for(size_t i=human_data[di-1].ndata;i<human_data[di].ndata;i++) {
							data_lls(i)  = hypotheses[h].compute_single_likelihood((*human_data[di].data)[i]);
							decay_pos(i) = human_data[di].decay_position;
						}
					}
					else {
						// compute anew; if ndata=0 then we should just include a 0.0
						for(size_t i=0;i<human_data[di].ndata;i++) {
							data_lls(i) = hypotheses[h].compute_single_likelihood((*human_data[di].data)[i]);
							decay_pos(i) = human_data[di].decay_position;
						}				
					}
					
					// set as an Eigen vector in out
					#pragma omp critical
					LL->at(h,di) = std::make_pair(data_lls,decay_pos);
				}
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
			MX = std::max(MX, di.decay_position+1); // need +1 since 0 decay needs one value
		}
		
		// just compute this once -- should be faster to use vector intrinsics? 
		// we store these in reverse order from some max data size
		// so that we can just take the tail for what we need
		Vector powers = Vector::Ones(MX);
		for(int i=1;i<MX;i++) { // intentionally leaving powers(0) = 1 here
			powers(i) = powf(i,-decay);
		}
		
		// fix it if its the wrong size
		if(decayedLikelihood->rows() != C->rows() or
		   decayedLikelihood->cols() != (int)human_data.size()) {
			decayedLikelihood.reset(new Matrix(C->rows(), human_data.size()));
	    } // else we don't need to do anything since it' all overwritten below
		   
		// sum up with the memory decay
		//#pragma omp parallel for
		for(int h=0;h<C->rows();h++) {
			for(int di=0;di<decayedLikelihood->cols();di++) {
				const Vector& v = LL->at(h,di).first;
				const Vector  d = powers(LL->at(h,di).second.array());
				decayedLikelihood->operator()(h,di) = v.dot(d) / llt;							
				//CERR decayedLikelihood->operator()(h,di) ENDL;
			}
		}
	}
	
	/**
	 * @brief 
	 * @param hypotheses
	 * @param human_data
	 * @return 
	 */
	virtual void recompute_P(std::vector<HYP>& hypotheses, const data_t& human_data) {
		assert(which_data == std::addressof(human_data));
		
		P.reset(new Predict_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t di=0;di<human_data.size();di++) {	
			for(size_t h=0;h<hypotheses.size();h++) {			
				
				// we get back a discrete distribution, but we'd like to store it as a vector
				// so its faster to iterature
				auto ret = hypotheses[h](*human_data[di].predict);
				
				std::vector<std::pair<typename HYP::output_t,double>> v;
				v.reserve(ret.size());
				
				for(auto& x : ret.values()) {
					v.push_back(std::make_pair(x.first, exp(x.second)));
				}
				
				P->at(h,di) = v;
			}
		}
	}

	
	virtual double compute_prior() override {
		return this->prior = logA.compute_prior() + params.compute_prior();
	}
	
	/**
	 * @brief This returns a vector where the i'th element is the normalized posterior probability
	 * @return 
	 */	
	virtual Matrix compute_normalized_posterior() {
		
		Vector hprior = hypothesis_prior(*C) / pt; 
		
		// do we need to normalize the prior here? The answer is no -- because its just a constant
		// and that will get normalized away in posterior
		
		Matrix hposterior = decayedLikelihood->colwise() + hprior; // the model's posterior
		
		// now normalize it and convert to probabilities
		#pragma omp parallel for
		for(int i=0;i<hposterior.cols();i++) { 
			// here we normalize and convert it to *probability* space
			hposterior.col(i) = lognormalize(hposterior.col(i)).array().exp();
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
			CERR "*** You are computing likelihood on a dataset that the GrammarHypothesis was not constructed with." ENDL
			CERR "		You must call set_hypotheses_and_data before calling this likelihood, but note that when you" ENDL
			CERR "      do that, it will need to recompute everything (which might take a while)." ENDL;
			assert(false);
		}		

		// Ok this needs a little explanation. IF we have overwritten the types, then we can get compilation
		// errors below because for instance r.first won't be of the right type to index into model_predictions.
		// So this line catches that and removes this chunk of code at compile time if we have removed
		// those types. In its place, we add a runtime assertion fail, meaning you should have overwritten
		// compute_likelihood if you change these types
		if constexpr(std::is_same<std::map<typename HYP::output_t,size_t>, typename datum_t::response_t>::value) {
			
			Matrix hposterior = compute_normalized_posterior();
			
			this->likelihood  = 0.0; // of the human data
			#pragma omp parallel for
			for(size_t i=0;i<human_data.size();i++) {
				
				// the non-log version. Here we avoid computing ang logs, logsumexp, or even exp in this loop
				// because it is very slow. 
				std::map<typename HYP::output_t, double> model_predictions;
				for(int h=0;h<hposterior.rows();h++) {
					if(hposterior(h,i) < 1e-6)  continue;  // skip very low probability for speed
						
					for(const auto& mp : P->at(h,i)) {						
						float p = hposterior(h,i) * mp.second;
						// map 0 for nonexisting doubles
						model_predictions[mp.first] += p;
					}
				}
				
				
				double ll = 0.0; // the likelihood here
				auto& di = human_data[i];
				for(const auto& r : di.responses) {
					ll += log( (1.0-alpha)*di.chance + alpha*model_predictions[r.first]) * r.second; // log probability times the number of times they answered this
				}
				
				#pragma omp critical
				this->likelihood += ll;
			}
		
			return this->likelihood;	
		}
		else {
			assert(false && "*** If you use GrammarHypothesis with non-default types, you need to overwrite compute_likelihood so it does the right thing.");
		}
	
	}
	
	[[nodiscard]] virtual this_t restart() const override {
		this_t out(*static_cast<const this_t*>(this)); // copy
		
		out.logA = logA.restart();
		out.params = params.restart();	

		out.recompute_alpha();
		out.recompute_llt();
		out.recompute_decay();
		out.recompute_pt();
		
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
	[[nodiscard]] virtual std::pair<this_t,double> propose() const override {
		this_t out(*static_cast<const this_t*>(this)); // copy
		
		if(flip(0.1)){
			auto [ p, fb ] = params.propose();
			out.params = p;
			
			// figure out what we need to recompute
			if(p(0) != params(0)) out.recompute_alpha();
			if(p(1) != params(1)) out.recompute_llt();
			if(p(2) != params(2)) out.recompute_decay();
			if(p(3) != params(3)) out.recompute_pt();
			
			if(p(1) != params(1) or p(2) != params(2))	{// note: must come after its computed
				out.recompute_decayedLikelihood(*out.which_data); // must recompute this when we get to likelihood
			}			
			return std::make_pair(out, fb);
		}		
		else {
			auto [ h, fb ] = logA.propose();
			out.logA = h;
			return std::make_pair(out, fb);
		}
	}
	
	/**
	 * @brief 
	 * @param C
	 * @return 
	 */	
	virtual Vector hypothesis_prior(Matrix& myC) {
		// take a matrix of counts (each row is a hypothesis)
		// and return a prior for each hypothesis under my own X
		// (If this was just a PCFG, which its not, we'd use something like lognormalize(C*proposal.getX()))

		Vector out(nhypotheses()); // one for each hypothesis
		
		// get the marginal probability of the counts via  dirichlet-multinomial
		Vector allA = logA.value.array().exp(); // translate [-inf,inf] into [0,inf]
		
		#pragma omp parallel for
		for(auto i=0;i<myC.rows();i++) {
			
			double lp = 0.0;
			size_t offset = 0; // 
			for(size_t nt=0;nt<grammar->count_nonterminals();nt++) { // each nonterminal in the grammar is a DM
				size_t nrules = grammar->count_rules( (nonterminal_t) nt);
				if(nrules > 1) { // don't need to do anything if only one rule (but must incremnet offset)
					Vector a = eigenslice(allA,offset,nrules); // TODO: seqN doesn't seem to work with this eigen version
					Vector c = eigenslice(myC.row(i),offset,nrules);
					double n = a.sum(); assert(n > 0.0); 
					double c0 = c.sum();
					if(c0 != 0.0) { // TODO: Check this...  
						// NOTE: std::lgamma here is not thread safe, but by my read it should be when the argument is positive
						lp += std::lgamma(n+1) + std::lgamma(c0) - std::lgamma(n+c0) 
							 + (c.array() + a.array()).array().lgamma().sum() 
							 - (c.array()+1).array().lgamma().sum() 
							 - a.array().lgamma().sum();
					}
				}
				offset += nrules;
			}
		
			out(i) = lp;
		}		
		
		return out;		
	}
	
	virtual bool operator==(const this_t& h) const override {
		return (C == h.C and LL == h.LL and P == h.P) and 
				(logA == h.logA) and (params == h.params);
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
		out += prefix + "-1\talpha" +"\t"+ str(alpha) + "\n";
		out += prefix + "-1\tllt" +"\t"+ str(llt) + "\n";
		out += prefix + "-1\tdecay" +"\t"+ str(decay) + "\n";
		out += prefix + "-1\tpt" +"\t"+ str(pt) + "\n";
		
		// now add the grammar operations
		size_t xi=0;
		for(auto& r : *grammar) {
			if(logA.can_propose[xi]) { // we'll skip the things we set as effectively constants (but still increment xi)
				std::string rs = r.format;
				rs = std::regex_replace(rs, std::regex("\\%s"), "X");
				out += prefix + str(r.nt) + "\t" + QQ(rs) +"\t" + str(exp(logA(xi))) + "\n"; // unlogged here
			}
			xi++;
		}	
		
		return out;	
	}
	
	/**
	 * @brief Need to override print since it will print in a different format
	 * @return 
	 */	
	virtual void print(std::string prefix="") override {
		COUT string() ENDL;
	}
	
	virtual size_t hash() const override { 
		size_t output = logA.hash();
		hash_combine(output, params.hash());
		return output;
	}
	
};
