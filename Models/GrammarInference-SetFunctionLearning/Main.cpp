

// TODO: Might be easier to have responses be to the entire list of objects?

#include <assert.h>
#include <set>
#include <string>
#include <tuple>
#include <regex>
#include <vector>
#include <tuple>
#include <utility>
#include <functional>
#include <cmath>

#include "EigenLib.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Set up primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Define this so we don't import its main function
#define NO_FOL_MAIN 1

#include "../FirstOrderLogic/Main.cpp"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// I must declare my own in order to fill in the this_t in the template
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "GrammarHypothesis.h" 

class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis>;
	using Super::Super;
};


size_t grammar_callback_count = 0;
void gcallback(MyGrammarHypothesis& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT h.string(str(grammar_callback_count)+"\t");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
#include "Top.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"

#include "Data.h"

const double alpha = 0.95; // in learning

S hypothesis_path = "hypotheses.txt";
S runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

int main(int argc, char** argv){ 	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.initialize(argc, argv);
	
	MyGrammar grammar(PRIMITIVES);
	
	//------------------
	// set up the data
	//------------------
	
	std::vector<HumanDatum<MyHypothesis>> human_data;
	
	// We need to read the data and collapse same setNumbers and concepts to the same "set", since this is the new
	// data format for the model. 
	std::ifstream infile("preprocessing/data.txt");
	
	MyHypothesis::data_t* learner_data = nullptr; // pointer to a vector of learner data
	
	// what data do I run mcmc on? not just human_data since that will have many reps
	// NOTE here we store only the pointers to sequences of data, and the loop below loops
	// over items. This lets us preserve good hypotheses across amounts of data
	// but may not be as great at using parallel cores. 
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	size_t ndata = 0;
	int    decay_position = 0;
	S prev_conceptlist = ""; // what was the previous concept/list we saw? 
	size_t LEANER_RESERVE_SIZE = 512; // reserve this much so our pointers don't break;
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		// use the helper above to read the next chunk of human data 
		// (and put the file pointer back to the start of the next chunk)
		auto [objs, corrects, yeses, nos, conceptlist] = get_next_human_data(infile);
		// could assert that the sizes of these are all the same
		
		// if we are on a new conceptlist, then make a new learner data (on stack)
		if(conceptlist != prev_conceptlist) {
			if(learner_data != nullptr) 
				mcmc_data.push_back(learner_data);
			
			// need to reserve enough here so that we don't have to move -- or else the pointers break
			learner_data = new MyHypothesis::data_t();
			learner_data->reserve(LEANER_RESERVE_SIZE);		
			ndata = 0;
			decay_position = 0;
		}
		
		// now put the relevant stuff onto learner_data
		
		for(size_t i=0;i<objs->size();i++) {
			MyInput inp{objs->at(i), *objs};
			learner_data->emplace_back(inp, (*corrects)[i], alpha);
			assert(learner_data->size() < LEANER_RESERVE_SIZE);
		}
				





		// TODO: CHECK IF THIS IS RIGHT WE SHOULD HAVE AN EMPTY element to start


		
				
		// now unpack this data into human_data for each point in the concept
		for(size_t i=0;i<objs->size();i++) {
			// make a map of the responses
			std::map<bool,size_t> m; m[true] = (*yeses)[i]; m[false] = (*nos)[i];
			
			HumanDatum<MyHypothesis> hd{learner_data, ndata, &((*learner_data)[ndata+i].input), std::move(m), 0.5, decay_position};
		
			human_data.push_back(std::move(hd));
		}
		
		ndata += objs->size();
		decay_position++;
		prev_conceptlist = conceptlist;	
	}
	if(learner_data != nullptr) 
		mcmc_data.push_back(learner_data); // and add that last dataset
	
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	std::vector<MyHypothesis> hypotheses; 
	if(runtype == "hypotheses" or runtype == "both") {
		auto h0 = MyHypothesis::make(&grammar); 
		hypotheses = get_hypotheses_from_mcmc(h0, mcmc_data, Control(FleetArgs::inner_steps, FleetArgs::inner_runtime), FleetArgs::ntop);
		CTRL_C = 0; // reset control-C so we can break again for the next mcmc
		
		save(hypothesis_path, hypotheses);
	}
	else {
		// only load if we haven't run 
		hypotheses = load<MyHypothesis>(hypothesis_path, &grammar);
	}
	COUT "# Hypothesis size: " TAB hypotheses.size() ENDL;
	assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
	if(runtype == "grammar" or runtype == "both") { 
		
		auto h0 = MyGrammarHypothesis::make(hypotheses, &human_data);
	
		tic();
		auto thechain = MCMCChain(h0, &human_data, &gcallback);
		thechain.run(Control());
		tic();
	}
	
}