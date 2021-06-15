
// We will define this so we can read from the NumberGame without main
#define DO_NOT_INCLUDE_MAIN 1

#include "../FormalLanguageTheory-Simple/Main.cpp"

#include "Data/HumanDatum.h"
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"
#include "Coroutines.h"

std::string hypothesis_path = "hypotheses.txt";
std::string runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

// The format of data for grammar inferece
using MyHumanDatum = HumanDatum<MyHypothesis>;

#include "GrammarHypothesis.h"

// Define a grammar inference class -- nothing special needed here
class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using data_t = Super::data_t;		
	
};


int main(int argc, char** argv){ 	
	Fleet fleet("An example of grammar inference for formal languages");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.initialize(argc, argv);

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
		auto m = string_to<std::map<std::string,unsigned long>>(all_responses);
		
		// This glom thing will use anything overlapping in mcmc_data, and give
		// us a new pointer if it can. This decreases the amount we need to run MCMC search
		// on and saves memory
		glom(mcmc_data, this_data); 	// TODO: Change to reference to pointer?
		
		// now just put into the data
		human_data.push_back(MyHumanDatum{.data=this_data, 
										  .ndata=ndata, 
										  .predict=const_cast<S*>(&EMPTY_STRING), 
										  .responses=m,
										  .chance=1e-6, // TODO: UPDATE ~~~~~~
										  .decay_position=decay_pos,
										  .my_decay_position=i-1
										  });
		

	}	
	
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	///////////////////////////////
	// Run a search over hypotheses
	///////////////////////////////	
	
	std::vector<MyHypothesis> hypotheses; 
	if(runtype == "hypotheses" or runtype == "both") {
		
		auto h0 = MyHypothesis::sample(); 
		hypotheses = get_hypotheses_from_mcmc(h0, mcmc_data, Control(FleetArgs::inner_steps, FleetArgs::inner_runtime), FleetArgs::ntop);
		CTRL_C = 0; // reset control-C so we can break again for the next mcmc
		
		save(hypothesis_path, hypotheses);
	}
	else {
		hypotheses = load<MyHypothesis>(hypothesis_path, &grammar); // only load if we haven't run 
	}
	COUT "# Hypothesis size: " TAB hypotheses.size() ENDL;
	assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
	
	///////////////////////////////
	// Run the grammar inferece
	///////////////////////////////	
	
	if(runtype == "grammar" or runtype == "both") { 

		TopN<MyGrammarHypothesis> topMAP(1); // keeps track of the map

		auto h0 = MyGrammarHypothesis::make(hypotheses, &human_data);

		auto thechain = MCMCChain<MyGrammarHypothesis>(h0, &human_data);
		for(auto& h : thechain.run(Control()) | topMAP | print (FleetArgs::print) | thin(FleetArgs::thin) ){
			COUT h.string(str(thechain.samples)+"\t");
		}		
		
		// Now when we're done, show the model predicted outputs on the data
		// We'll use the MAP grammar hypothesis for this
		GrammarHypothesis MAP = topMAP.best();
		
		Matrix hposterior = MAP.compute_normalized_posterior(); // this is shared in the loop below, so computed separately		
		#pragma omp parallel for
		for(size_t i=0;i<human_data.size();i++) {
			auto model_predictions = MAP.compute_model_predictions(i, hposterior);		
			
			#pragma omp critical
			for(const auto& r : model_predictions) {
				COUT i TAB Q(r.first) TAB r.second ENDL;
			}
		}
	}
	
}