/*
 * 	TODO: This likelihood is not quite right because people in the experiment are sampling
 *        without replacement.
 * */
 
// We will define this so we can read from the NumberGame without main
#define DO_NOT_INCLUDE_MAIN 1

#include "../FormalLanguageTheory-Complex/Main.cpp"
const size_t MAX_FACTORS = 3;

#include "Data/HumanDatum.h"
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"
#include "Coroutines.h"

std::string hypothesis_path = "hypotheses.txt";
std::string runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

std::vector<size_t> ntops = {1,5,10,25,100,250,500,1000}; // save the top this many from each hypothesis

// The format of data for grammar inferece
using MyHumanDatum = HumanDatum<MyHypothesis>;

#include "GrammarHypothesis.h"

// Define a grammar inference class -- nothing special needed here
class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using data_t = Super::data_t;		
	
	virtual double human_chance_lp(const typename datum_t::output_t& r, const datum_t& hd) const override {
		// here we are going to make chance be exponential in the length of the response
		return -(double)r.length()*log(alphabet.size()); // NOTE: Without the double case, we negate r.length() first and it's awful
	}
};


int main(int argc, char** argv){ 	
	
	alphabet = "abcd"; // set this as the default. 
	
	Fleet fleet("An example of grammar inference for formal languages");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.initialize(argc, argv);

	// Add to the grammar

	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 10.0/alphabet.length());
	}

	// this is slightly non-ideal because e.g. F0 will have a different role
	// when there are 1 vs 2 vs 3 factors, but they all have the same prior here.
	for(size_t a=0;a<MAX_FACTORS;a++) {	
		auto s = std::to_string(a);
		double p = 1.0/(4*nfactors); // NOTE: Slightly different prior

		grammar.add(S("F")+s+"(%s)" ,  Builtins::Recurse<MyGrammar>, p, a);
		grammar.add(S("Fm")+s+"(%s)",  Builtins::MemRecurse<MyGrammar>, p, a);

		grammar.add(S("Fs")+s+"(%s)",  Builtins::SafeRecurse<MyGrammar>, p, a);
		grammar.add(S("Fms")+s+"(%s)", Builtins::SafeMemRecurse<MyGrammar>, p, a);
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
		size_t ndata = 0; // how many data points?
		int i=0;
		for(auto& s : string_to<std::vector<S>>(stimulus)) { // convert to a string vector and then to data
			MyHypothesis::datum_t d{.input="", .output=s};
			this_data->push_back(d);
			ndata = this_data->size(); // note that below we might change this_data's pointer, but we still need this length
			decay_pos->push_back(i++);
			
			// add a check that we're using the right alphabet here
			for(auto& c: s) assert(contains(alphabet,c));
		}
		
		// process all_responses into a map from strings to counts
		auto m = string_to<std::vector<std::pair<std::string,unsigned long>>>(all_responses);
		
		// This glom thing will use anything overlapping in mcmc_data, and give
		// us a new pointer if it can. This decreases the amount we need to run MCMC search
		// on and saves memory
		glom(mcmc_data, this_data); 	
		
		// now just put into the data
		human_data.push_back(MyHumanDatum{.data=this_data, 
										  .ndata=ndata, 
										  .predict=const_cast<S*>(&EMPTY_STRING), 
										  .responses=m,
										  .chance=1e-30, // TODO: UPDATE ~~~~~~
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
		
			auto h0 = MyHypothesis::sample(nf); 
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
		COUT "# Hypothesis size: " TAB hypotheses.size() ENDL;
		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
		// store the best
		TopN<MyGrammarHypothesis> topMAP(1); // keeps track of the map
	
		// the MCMC chain we run over grammars
		auto thechain = MCMCChain<MyGrammarHypothesis>(MyGrammarHypothesis::sample(hypotheses, &human_data), &human_data);
		
		// Main MCMC running loop!
		size_t nloops = 0;
		for(const auto& h : thechain.run(Control()) | topMAP | print(FleetArgs::print) | thin(FleetArgs::thin) ) {
			
			{
				std::ofstream outsamples(FleetArgs::output_path+"/samples.txt", std::ofstream::app);
				outsamples << h.string(str(thechain.samples)+"\t") ENDL;
				outsamples.close();
			}
			
			// Every this many steps (AFTER thinning) we print out the model predictions
			if(nloops++ % 100 == 0) {
				
				// Now when we're done, show the model predicted outputs on the data
				// We'll use the MAP grammar hypothesis for this
				auto& MAP = topMAP.best();
				const Matrix hposterior = MAP.compute_normalized_posterior(); 
				
				PRINTN("#", h.posterior ); 
				
				std::ofstream outMAP(FleetArgs::output_path+"/MAP-strings.txt");
				std::ofstream outtop(FleetArgs::output_path+"/top-H.txt");
				
				#pragma omp parallel for
				for(size_t i=0;i<human_data.size();i++) {
					auto& hd = human_data[i];
					
					// compute model predictions on each data point
					auto model_predictions = MAP.compute_model_predictions(i, hposterior);		
					
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
						outtop << "# " << cll.string() ENDL;
						outtop << i TAB max_hi TAB max_post TAB lse TAB QQ(mapH.string()) TAB QQ(str(slice(*hd.data, 0, hd.ndata))) ENDL;
						
						// get all of the strings output by people or the model
						std::set<std::string> all_strings;
						for(const auto& [s,p] : model_predictions) {
							all_strings.insert(s);
							UNUSED(p);
						}
						
						std::map<std::string, size_t> m; // make a map of outputs so we can index easily below
						size_t N = 0; // total number
						for(const auto& [s,c] : hd.responses)  {
							all_strings.insert(s);
							N += c;
							m[s] = c;
						}
					
						for(const auto& s : all_strings) {
							outMAP << i TAB 
									   Q(s) TAB 
									   get(model_predictions, s, 0) TAB 
									   get(m, s, 0) TAB  
									   N ENDL;
						}
					}
					
				}
				outMAP.close();
						
			}
			
		}		
		
		
	}
	
}