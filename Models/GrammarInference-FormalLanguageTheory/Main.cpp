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

#include "MyGrammarHypothesis.h"

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
	for(auto [row, stimulus, all_responses] : read_csv<3>("train-data.csv", '\t')) {
		
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
			for(size_t i=0;i<nf;i++) {
				h0.factors[i] = InnerHypothesis::sample();
			}
			
			auto hyps = get_hypotheses_from_mcmc(h0, mcmc_data, InnerControl(), ntops);
		
			for(size_t t=0;t<ntops.size();t++) {
				all_hypotheses[t].insert(hyps[t].begin(), hyps[t].end() );
			}
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
		print("# Hypothesis size: ", hypotheses.size(), std::addressof(hypotheses));
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
		for(const auto& h : thechain.run(Control()) | MAPGrammar | printer(FleetArgs::print) | thin(FleetArgs::thin) ) {
			
			{
				std::ofstream outsamples(FleetArgs::output_path+"/samples.txt", std::ofstream::app);
				outsamples << h.string(str(thechain.samples)+"\t") ENDL;
				outsamples.close();
			}
			
			// Now when we're done, show the model predicted outputs on the data
			// We'll use the MAP grammar hypothesis for this
			auto& MAP = MAPGrammar.best();
			const auto hposterior = MAP.compute_normalized_posterior(); 
			
			
			
			{
				std::ofstream outprior(FleetArgs::output_path+"/MAP-prior.txt");
				auto v = MAP.hypothesis_prior(*MAP.C);
				for(size_t hi=0;hi<MAP.nhypotheses();hi++){
					OUTPUTN(outprior, hi, v[hi], hypotheses[hi].compute_prior(), QQ(hypotheses[hi].string()));
				}
			}
			
			{
				std::ofstream outstrings(FleetArgs::output_path+"/strings.txt");
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
						
							
						// Let's get the model predictions for the most likely human strings 
						std::map<std::string, double> m; // make a map of outputs so we can index easily below
						for(const auto& [s,p] : model_predictions) { m[s] = p; }
						size_t N = 0; // total number
						for(const auto& [s,c] : hd.responses)  { N += c; }
						
						for(const auto& [s,c] : hd.responses)  {
							if(c > 5) {  // all stirngs with at least this many counts
								OUTPUTN(outstrings, i, QQ(s), get(m,s,0), c, N);
							}
						}
					
					}
					
				}
				outstrings.close();
				outtop.close();
			}
			
			// store total posterior if we leave out each primitive 
//				std::ofstream outLOO(FleetArgs::output_path+"/LOO-scores.txt");
//				for(nonterminal_t nt=0;nt<grammar.nts();nt++) {
//					for(auto& r : grammar.rules[nt]){
//					
//						MyHypothesis G = MAPGrammar; // copy the MAP
//						G.compute_posterior()
//					}
//				}
			
		} // end mcmc		
		
		
	} // end runtype 
	
	if(runtype == "compare") {
		
		// In this version, we run each hypothesis through the human data and 
		// see what training on the human data data would give as the most likely 
		// hypothesis, as compared to the training data. 
		
		auto hypotheses = load<MyHypothesis>(hypothesis_path);
		print("# Hypothesis size: ", hypotheses.size(), std::addressof(hypotheses));
		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
		#pragma omp parallel for
		for(size_t hdi=0;hdi<human_data.size();hdi++) {
			if(!CTRL_C) {
			
				const auto& d = human_data[hdi];
				
				// convert to ordinary data
				MyHypothesis::data_t hdata; // human data as training
				for(auto& [s,cnt] : d.responses) {
					hdata.push_back({.input=EMPTY_STRING, .output=s, .count=double(cnt)});
				}
				
				TopN<MyHypothesis> best_fromhuman(1);
				for(auto h : hypotheses) {  // make a copy
					if(CTRL_C) break;
					h.compute_posterior(hdata);
					best_fromhuman << h;
				}
				
				TopN<MyHypothesis> best_fromdata(1);
				auto learningdata = slice(*d.data, 0, (int)d.ndata);
				for(auto h : hypotheses) {  // make a copy
					if(CTRL_C) break;
					h.compute_posterior(learningdata);
					best_fromdata << h;
				}
				
				#pragma omp critical
				print(hdi, QQ(str(learningdata)), 
							QQ(best_fromdata.best().string()),
							QQ(str(d.responses)), 
							QQ(best_fromhuman.best().string()) 
							);
			}
		}
	
	}
	
}
