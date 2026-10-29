#pragma once 

#include "EigenLib.h"
#include "Numerics.h"

#include "Interfaces/MCMCable.h"

#include "HumanDatum.h"
#include "HumanDataBundle.h"
#include "Gamma.h"
#include "VectorOfHypotheses.h"


template<typename this_t,
         typename HYP,
		 typename HD, // human datum type
		 typename _datum_t = NullDatum_t, 
		 typename _data_t=std::span<_datum_t>
		 >
class NewGrammarHypothesis : public MCMCable<this_t, _datum_t, _data_t> {
public:
	using ConditionOn_t = typename HD::ConditionOn_t;
	using Response_t    = typename HD::Response_t;
	using HumanDataBundle_t = HumanDataBundle<HYP,HD>;
	using Hinput_t = typename HYP::input_t;
	using CleanHYP = std::remove_cvref_t<HYP>; // needed below for checking 
	
	std::shared_ptr<std::vector<HYP>> hypotheses;
	std::shared_ptr<Matrix>    C; // matrix of counts for eahc rule and hypothesis	
	std::shared_ptr<std::vector<HumanDataBundle_t>> human_data;
	
	// as a diagnostic, we will include automatically binning
	// by model prediction (i.e in [0,1] by BIN_SIZE) the counts of
	// hypothesis responses. This is recomputed on each likelihood computation
	float BIN_SIZE = 0.05; 
	std::vector<int> bin_yes;
	std::vector<int> bin_tot;
	
	std::shared_ptr<std::map<const Rule*, size_t>> rule_indexer; // for checking whcih rule for each -- set in recompute_C()
	
	// TODO: These should be static consts but then operator= complains
	size_t N_RULES;
	double HUMAN_DATA_OUTLIER = -99;

	// Parameteters that are fit
	Gamma people_PT; // people's prior temperature (e.g. when they make inferences)
	Gamma people_LLT; // people's likelihood temperature (e.g. when they make inferences)
	Gamma decay;  // expoential decay parameter
	VectorOfHypotheses<Gamma> logA; // grammar parameters

	NewGrammarHypothesis(std::vector<HYP>& h) :
		NewGrammarHypothesis( std::make_shared<std::vector<HYP>>(h) ) // NOTE: copies on the heap
	{
			
	}
	
	NewGrammarHypothesis(std::shared_ptr<std::vector<HYP>> hptr) : 
		hypotheses(hptr),
		human_data(std::make_shared<std::vector<HumanDataBundle_t>>()),
		N_RULES(hypotheses->at(0).get_grammar()->count_rules()),
		people_PT(5,5), // some slightly informative priors about all of these components 
		people_LLT(5,5),
		decay(5,5),		
		logA(N_RULES, 5, 5)
	{	
		
		// just compute our counts 
		recompute_C();
	}
	
	void add_HumanData(ConditionOn_t cond, 
					   Hinput_t gen, 
					   Response_t& resp, 
					   std::vector<float> decays ) {
						   
		human_data->emplace_back(*hypotheses, cond, gen, resp, decays);
	}
	
	void add_HumanData(HD& d) {
		human_data->emplace_back(*hypotheses, d);
	}
	
	void add_HumanData(std::vector<HD>& data) {
		
		// here if we add all the data at once we can reserve and do it in parallel
		human_data->resize(data.size());
		
		#pragma omp parallel for
		for(size_t i=0;i<data.size();i++) {
			human_data->at(i) = HumanDataBundle(*hypotheses, data.at(i));
		}
	}
	

	/**
	 * @brief Computes our matrix C[h,r] of hypotheses (rows) by counts of each grammar rule.
	 *			(requires that each hypothesis use the same grammar)
	 * @param hypotheses
	 */
	virtual void recompute_C() {
		
		const auto H = hypotheses->size();
		assert(H > 0);

		auto g = hypotheses->at(0).get_grammar();
		auto nRules = g->count_rules();

		C.reset(new Matrix(H, nRules)); 

		rule_indexer = std::make_shared<std::map<const Rule*, size_t>>(g->get_rule_indexer()); // map rules to 
		const auto R = g->count_rules();

		#pragma omp parallel for
		for(size_t i=0;i<H;i++) {
			assert(hypotheses->at(i).get_grammar() == g); // this only works if they have the same grammar, since they share an indexer
			
			// hypotheses here are factors so we have to sum them
			Vector cv = Vector::Zero(R);
			
			auto c = g->get_counts(hypotheses->at(i).get_value(), *rule_indexer); // extract counts using indexer
			for(size_t r=0;r<R;r++)  // update cv 
				cv[r] += c[r];
			
			#pragma omp critical
			C->row(i) = std::move(cv);
		}
		
		assert( (size_t)C->rows() == H);
	}
	
	virtual Vector hypothesis_prior() const {

		// just convert VectorOfHypotheses to a Vector of floats
		assert(logA.size() == N_RULES);
		Vector la(N_RULES); 
		for(size_t i=0;i<N_RULES;i++) 
			la(i) = (float)logA.at(i);
		
		// now its a simple dot-product, scaling by temperature
		return ((*C) * (-la)) / double(people_PT);	
	}
		
	virtual double compute_likelihood() {
		
		// the likelihood is easy to compute in this simple case:
		// (otherwise it be implemented by a subclass)
		if constexpr (std::is_same_v<Response_t, 
									 std::map<typename HYP::output_t, int>>) {
											
			// each hypothesis's			  prior
			const auto priors = this->hypothesis_prior(); // determined by counts and parameters
			const auto H = hypotheses->size();
			
			// reset the bins
			bin_yes = std::vector<int>(ceil(1.0/BIN_SIZE), 0);
			bin_tot = std::vector<int>(ceil(1.0/BIN_SIZE), 0);
			
			this->likelihood = 0.0; // easier to just compute here
			
			#pragma omp parallel for
			for(size_t di=0;di<human_data->size();di++){
				auto& hd = human_data->at(di);
				
				// get each data point's marginal over responses
				Vector posteriors(H);
				for(size_t hi=0;hi<H;hi++) {
					posteriors(hi) =  priors(hi) + 
									  hd.likelihood_with_decay(hi, decay) / double(people_LLT); // likelihood temperatures here
				}
				
				// Normalized posteriors on this data
				const Vector lpost = lognormalize(posteriors).array();
				
				
				// compute the marginal model predictions (marginalizing over hypotheses)
				DiscreteDistribution<typename HYP::output_t> model_pr;
				for(size_t hi=0;hi<H;hi++) {
					model_pr.addmass(hd.hypotheses_responses.at(hi), lpost(hi)); 
				}
					
				double myll = 0.0;
				for(auto& [r,cnt] : hd.human_responses) {
					myll +=  cnt * model_pr.get(r, HUMAN_DATA_OUTLIER);
				}
				
				// TODO: Do not do this in parallel -- because we are tracking bins etc
				#pragma omp critical
				{
					// look at bins by model and see proportion of the time you get a yes by model bin							
					int z = 0;
					for (const auto& [_, value] : hd.human_responses) { z += value; }
					for(auto& [r, mp] : model_pr.values()) {
						size_t b = floor(exp(mp)*bin_tot.size());
						bin_yes[b] += get(hd.human_responses, r, 0);
						bin_tot[b] += z; 					
					}
					
					// and add the likelihood
					this->likelihood += myll;
				}
				
			}
			
			return this->likelihood; 
		 }
		 else {
			 assert(false && "*** Must implement compute_likelihood for custom human respones types");
		 }
	}

	// wrapper to get the terface right
	virtual double compute_likelihood(const std::span<NullDatum_t>& null_data, double breakout=-infinity) override {
		assert(null_data.size() == 0); // we don't have data
		return compute_likelihood();
	}	
	
	// For MCMCable
	[[nodiscard]] virtual this_t restart() const override {
		this_t out(*static_cast<const this_t*>(this)); // copy
		
		out.logA = logA.restart();
		out.people_LLT = people_LLT.restart();
		out.people_PT = people_PT.restart();
		out.decay = decay.restart();
		
		return out;
	}
	
	virtual size_t hash() const override { 
		size_t output = logA.hash();
		hash_combine(output, people_LLT.hash(), people_PT.hash(), decay.hash());
		return output;
	}
	
	
	[[nodiscard]] virtual std::optional<std::pair<this_t,double>> propose() const override {
		
		// TODO: Can probably speed this up by not making the copy if the proposal fails
		
		// make a copy
		this_t out(*static_cast<const this_t*>(this)); 
		
		if(flip(0.15)){ // TODO: if flat, we NEVER propose to logA
			
			double myfb = 0.0;
			auto which = random_nonempty_subset(3, 0.25);
	
			if(which.at(0)) {
				auto p = people_PT.propose();
				if(not p) return {}; // Hmm can change this -- failed proposals can just be skipped here
				auto [ v, fb ] = p.value();
				out.people_PT = v;
				myfb += fb;
			}
			
			if(which.at(1)) {
				auto p = decay.propose();
				if(not p) return {};
				auto [ v, fb ] = p.value();
				out.decay = v;
				myfb += fb;
			}
			if(which.at(2)) {
				auto p = people_LLT.propose();
				if(not p) return {};
				auto [ v, fb ] = p.value();
				out.people_LLT = v;
				myfb += fb;
			}
			
			return std::make_pair(out, myfb);
		}		
		else {
			auto p = logA.propose();
			if(not p) return {};
			auto [ v, fb ] = p.value();
			out.logA = v;
			return std::make_pair(out, fb);
		}
	}	
	
	virtual double compute_prior() override {
		return this->prior = people_LLT.compute_prior() + 
							 logA.compute_prior() + 
							 people_PT.compute_prior() +
							 decay.compute_prior();
	}
	
	virtual bool operator==(const this_t& h) const override {
			return (C == h.C) and 
				(people_LLT == h.people_LLT) and
				(logA == h.logA)   and 
				(people_PT == h.people_PT) and
				(decay == h.decay);
	}
	
	/**
	 * @brief This returns a string for this hypothesis. Defaulty, now, just in tidy format
	 * 		  with all the parameters, one on each row. Note these parameters are shown after
	 * 		  transformation (e.g. the prior parameters are NOT logged)
	 * @return 
	 */	
	virtual std::string string(std::string prefix="") const override {
		std::string out = "";

		out += "# " + str(bin_yes) + "\n"; 
		out += "# " + str(bin_tot) + "\n"; 
		
		// the -1 here is just to contrast with the nt printed below
		out += prefix + "-1\tposterior.score" +"\t"+ str(this->posterior) + "\n";
		out += prefix + "-1\tparameter.prior" +"\t"+ str(this->prior) + "\n";
		out += prefix + "-1\thuman.likelihood" +"\t"+ str(this->likelihood) + "\n";
		out += prefix + "-1\tpeople_LLT" +"\t"+ str((double)people_LLT) + "\n";
		out += prefix + "-1\tpeople_PT" +"\t"+ str((double)people_PT) + "\n";
		out += prefix + "-1\tdecay" +"\t"+ str((double)decay) + "\n";
		
		//auto g = hypotheses->at(0).get_grammar();
		
		// now add the grammar operations
		for(auto& [r,idx] : *rule_indexer) {
//			if(logA.can_propose[xi]) { // we'll skip the things we set as effectively constants (but still increment xi)
			std::string rs = r->format;
//			rs = std::regex_replace(rs, std::regex("\\%s"), "X");
			out += prefix + str(r->nt) + "\t" + QQ(rs) +"\t" + str((double)logA.at(idx)) + "\n"; // unlogged here
//			}
		}	
		out.erase(out.size()-1,1); // delete the final newline 
		
		return out;	
	}

	virtual void show(std::string prefix="") override {
		print(this->string(prefix));
	}

};

