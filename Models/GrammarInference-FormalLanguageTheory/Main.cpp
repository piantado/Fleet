/*
 * 	TODO: This likelihood is not quite right because people in the experiment are sampling
 *        without replacement.
 * 
 * 	Languages:
 * 	- FLT language
 *  - Simplified FLT
 *  - Dehaene
 * 	- Dehaene + subst
 *  - FLT plus subst
 * 
 * 
 * 	
 * */

#include <cstddef>
#include <vector>

const size_t MAX_FACTORS = 3;

#include "Data/HumanDatum.h"
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"
#include "Coroutines.h"

std::string alphabet = "abcd";
std::string hypothesis_path = "hypotheses.txt";
std::string runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

std::vector<size_t> ntops = {1,5,10,25,100,250,500,1000}; // save the top this many from each hypothesis

size_t PREC_REC_N   = 25;  // if we make this too high, then the data is finite so we won't see some stuff
static constexpr float alpha = 0.1; // probability of insert/delete errors (must be a float for the string function below)
size_t max_length = 128; // (more than 256 needed for count, a^2^n, a^n^2, etc -- see command line arg)
size_t max_setsize = 64; // throw error if we have more than this
size_t nfactors = 2; // how may factors do we run on? (defaultly)

const std::string errorstring = "";
const double CONSTANT_P = 3.0; // upweight probability for grammar constants. 

// These are necessary for MyHypothesis
size_t current_ntokens = 0; 
std::pair<double,double> mem_pr; 

std::string current_data = "";
std::string prdata_path = ""; 

using S = std::string; 
using StrSet = std::set<S>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// A few handy functions here
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

std::string substitute(std::string newA, std::string s) {
	if(newA.size() == 0) return s; // just don't do anything if missing alphabet
	for(size_t i=0;i<s.size();i++) {
		auto idx = alphabet.find(s[i]);
		s[i] = newA[idx % newA.size()]; // takes mod newA.size
	}
	return s;
}

#include "MyGrammar.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare an inner hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "../FormalLanguageTheory-Complex/Data.h"
#include "../FormalLanguageTheory-Complex/MyHypothesis.h"

MyHypothesis::data_t prdata; // used for computing precision and recall -- in case we want to use more strings?


// The format of data for grammar inferece
using MyHumanDatum = HumanDatum<MyHypothesis>;

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
				remove_strings_and_renormalize(M, dptr, max_sizes[dptr]);
				
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
			remove_strings_and_renormalize(M, human_data[i].data, human_data[i].ndata);
			
			for(const auto& [outcome,outlp] : M) {						
				model_predictions[outcome] += hposterior(h,i) * exp(outlp);
			}
		}
		
		return model_predictions;
	}
	
	
	
};

////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 	
	
	alphabet = "abcd"; // set this as the default. 
	
	Fleet fleet("An example of grammar inference for formal languages");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.initialize(argc, argv);

	// Add to the grammar
	for(size_t i=0;i<MAX_FACTORS;i++) {	
		grammar.add_terminal( str(i), (int)i, 1.0/MAX_FACTORS);
	}
	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, CONSTANT_P/alphabet.length());
	}
	
	///////////////////////////////
	// Read the human data
	///////////////////////////////	
	
	std::vector<MyHumanDatum> human_data;         // what do we run grammar inference on
	std::vector<MyHypothesis::data_t*> mcmc_data; // what will we run search over MyHypothesis on?
	for(auto [row, stimulus, all_responses] : read_csv<3>("human-data.csv", '\t')) {
		
		// split up stimulus into components
		auto this_data = new MyHypothesis::data_t();
		auto decay_pos = new std::vector<int>(); 
		int i=0;
		for(auto& s : string_to<std::vector<S>>(stimulus)) { // convert to a string vector and then to data
			this_data->push_back(MyHypothesis::datum_t{.input=EMPTY_STRING, .output=s});
			decay_pos->push_back(i++);
			
			// add a check that we're using the right alphabet here
			check_alphabet(s, alphabet);
		}
		size_t ndata = this_data->size(); // note that below we might change this_data's pointer, but we still need this length
			
		// process all_responses into a map from strings to counts
		auto m = string_to<std::vector<std::pair<std::string,unsigned long>>>(all_responses);
		
		// This glom thing will use anything overlapping in mcmc_data, and give
		// us a new pointer if it can. This decreases the amount we need to run MCMC search
		// on and saves memory; NOTE: This (often) changes this_data
		glom(mcmc_data, this_data); 	
		
		// now just put into the data
		human_data.push_back(MyHumanDatum{.data=this_data, 
										  .ndata=ndata, 
										  .predict=nullptr, // const_cast<S*>(&EMPTY_STRING), 
										  .responses=m,
										  .chance=NaN, // should not be used via human_chance_lp above
										  .decay_position=decay_pos,
										  .my_decay_position=i-1
										  });
	}
	
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	///////////////////////////////
	// Run a search over hypotheses
	///////////////////////////////	
	

	if(runtype == "hypotheses" or runtype == "both") {
		
		std::vector<std::set<MyHypothesis>> all_hypotheses(ntops.size());
		
		// here, we need to run on each number of factors
		for(size_t nf=1;nf<=MAX_FACTORS;nf++) {
		
			// Need to fix each of the recursive arguments to only use those up to nf
			// NOTE That we can't use grammar.clear_all because that will change the
			// counts of how things are aligned in the grammar inference			
			nonterminal_t nt = grammar.nt<int>();
			grammar.Z[nt] = 0.0; // reset 
			for(size_t i=0;i<MAX_FACTORS;i++) {	
				Rule* r = grammar.get_rule(nt, str(i));
				if(i < nf) {
					r->p = 1.0/nf; // change this rule -- warning, kinda dangerous
					grammar.Z[nt] += 1.0/nf;
				}
				else {
					r->p = 0.0; // set to be zero to assure it's never sampled. 
				}
			}
			
			auto h0 = MyHypothesis::sample({}); 
			for(size_t i=0;i<nf;i++) 
				h0.factors[i] = InnerHypothesis::sample();
			
			auto hyps = get_hypotheses_from_mcmc(h0, mcmc_data, InnerControl(), ntops);
		
			for(size_t t=0;t<ntops.size();t++) 
				all_hypotheses[t].insert(hyps[t].begin(), hyps[t].end() );
		}
		
		for(size_t t=0;t<ntops.size();t++) 
			save(FleetArgs::output_path+"."+str(ntops[t]), all_hypotheses[t]);
		
		CTRL_C = 0; // reset control-C so we can break again for the next mcmc		
	}

	
	///////////////////////////////
	// Run the grammar inference
	///////////////////////////////	
	
	if(runtype == "grammar" or runtype == "both") { 
		
		auto hypotheses = load<MyHypothesis>(hypothesis_path);
		PRINTN("# Hypothesis size: ", hypotheses.size(), std::addressof(hypotheses));
		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
		// store the best
		TopN<MyGrammarHypothesis> MAPGrammar(1); // keeps track of the map
	
		// initial hypothesis
		auto h0 = MyGrammarHypothesis::sample(hypotheses, &human_data);
		
		// lets force probabilities and integers to have fixed, uniform priors 
		{
			size_t i = 0;
			for(auto& r : grammar) {
				// don't do inference over numbers, alphabet (char), or probs (double)
				if(r.nt == grammar.nt<size_t>() or 
				   r.nt == grammar.nt<int>() or 
				   r.nt == grammar.nt<char>() or 
				   r.nt == grammar.nt<double>()) {					
					//contains(r.format, "/") or 
					// r.format.at(0)=='#') { 
					h0.logA.set(i, 0.0); // set the value to 1 (you MUST do this) 
					h0.set_can_propose(i, false);
				}
				i++;
			}
		}
		
		// the MCMC chain we run over grammars
		auto thechain = MCMCChain<MyGrammarHypothesis>(h0, &human_data);
		
		// Main MCMC running loop!
		for(const auto& h : thechain.run(Control()) | MAPGrammar | print(FleetArgs::print) | thin(FleetArgs::thin) ) {
			
			// Every this many steps (AFTER thinning) we print out the model predictions
			if(thechain.samples % 100 == 0) {
				
				{
					std::ofstream outsamples(FleetArgs::output_path+"/samples.txt", std::ofstream::app);
					outsamples << h.string(str(thechain.samples)+"\t") ENDL;
					outsamples.close();
				}
				
				// Now when we're done, show the model predicted outputs on the data
				// We'll use the MAP grammar hypothesis for this
				auto& MAP = MAPGrammar.best();
				const auto hposterior = MAP.compute_normalized_posterior(); 
				
				std::ofstream outMAP(FleetArgs::output_path+"/MAP-strings.txt");
				std::ofstream outtop(FleetArgs::output_path+"/top-H.txt");
				
				#pragma omp parallel for
				for(size_t i=0;i<human_data.size();i++) {
					auto& hd = human_data[i];
					
					// compute model predictions on each data point
					auto model_predictions = MAP.compute_model_predictions(human_data, i, hposterior);		
					
					// now figure out the full set of strings
					#pragma omp critical
					{
						// find the MAP model hypothesis for this data point
						double max_post = 0.0; 
						size_t max_hi = 0;
						double lse = -infinity;
						for(auto hi=0;hi<hposterior.rows();hi++) {
							lse = logplusexp(lse, (double)log(hposterior(hi,i)));
							if(hposterior(hi,i) > max_post) { 
								max_post = hposterior(hi,i); 
								max_hi = hi;
							}
						}					
						
						// store the top hypothesis found
						auto mapH = hypotheses[max_hi];
						auto cll = mapH.call("");
						OUTPUTN(outtop, "#", cll.string());
						OUTPUTN(outtop, i, max_hi, max_post, lse, QQ(mapH.string()), QQ(str(slice(*hd.data, 0, hd.ndata))));
						
						// get all of the strings output by people or the model
						std::set<std::string> all_strings;

						// model strings
						for(const auto& [s,p] : model_predictions) {
							if(p > 0.001) { // don't print things that are crazy low probability
								all_strings.insert(s);
							}
						}
						
						std::map<std::string, size_t> m; // make a map of outputs so we can index easily below
						size_t N = 0; // total number
						for(const auto& [s,c] : hd.responses)  {
							all_strings.insert(s);
							N += c;
							m[s] = c;
						}
					
						for(const auto& s : all_strings) {
							OUTPUTN(outMAP, i, Q(s), get(model_predictions, s, 0), get(m, s, 0), N);
						}
					}
					
				}
				outMAP.close();
				outtop.close();
				
				// store total posterior if we leave out each primitive 
//				std::ofstream outLOO(FleetArgs::output_path+"/LOO-scores.txt");
//				for(nonterminal_t nt=0;nt<grammar.nts();nt++) {
//					for(auto& r : grammar.rules[nt]){
//					
//						MyHypothesis G = MAPGrammar; // copy the MAP
//						G.compute_posterior()
//					}
//				}
						
			}
			
		}		
		
		
	}
	
}