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

#include "DeterministicGrammarHypothesis.h" 

class MyGrammarHypothesis final : public DeterministicGrammarHypothesis<MyGrammarHypothesis, MyHypothesis> {
public:
	using Super = DeterministicGrammarHypothesis<MyGrammarHypothesis, MyHypothesis>;
	using Super::Super;
	
	MyGrammarHypothesis(std::vector<HYP>& hypotheses, const data_t* human_data) {
		this->set_hypotheses_and_data(hypotheses, *human_data);
	}	
};


////////////////////////////////////////////////////////////////////////////////////////////
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"
#include "Vectors.h"
#include "Data.h"

std::vector<size_t> ntops = {1,5,10,25,100,250,500,1000}; // save the top this many from each hypothesis

const double alpha = 0.95; // in learning

S hypothesis_path = "hypotheses.txt";
S runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

int main(int argc, char** argv){ 	
	
	FleetArgs::input_path = "preprocessing/data.txt";
	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.initialize(argc, argv);
	
	//------------------
	// set up the data
	//------------------
	
	std::vector<HumanDatum<MyHypothesis>> human_data;
	std::vector<std::string> human_data_key; // store the raw strings so we can output more friendly
	
	std::vector<HumanDatum<MyHypothesis>> heldout_data;
	std::vector<std::string> heldout_data_key;
	
	// We need to read the data and collapse same setNumbers and concepts to the same "set", since this is the new
	// data format for the model. 
	std::ifstream infile(FleetArgs::input_path);
	
	MyHypothesis::data_t* learner_data = nullptr; // pointer to a vector of the current learner data
	std::vector<int>* decay_position = nullptr;
	
	// what data do I run mcmc on? not just human_data since that will have many reps
	// NOTE here we store only the pointers to sequences of data, and the loop below loops
	// over items. This lets us preserve good hypotheses across amounts of data
	// but may not be as great at using parallel cores. 
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	size_t ndata = 0;
	int    decay_position_counter = 0;
	S prev_conceptlist = "NA"; // what was the previous concept/list we saw? 
	size_t LEARNER_RESERVE_SIZE = 128; // reserve this much so our pointers don't break;
	size_t data_idx = 0; // what position are we overall for data
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		// use the helper to read the next chunk of human data 
		// (and put the file pointer back to the start of the next chunk)
		auto [objs, corrects, yeses, nos, conceptlist] = get_next_human_data(infile);
		// could assert that the sizes of these are all the same
		print("### -", conceptlist);
			
		// if we are on a new conceptlist, then make a new learner data (on stack)
		if(conceptlist != prev_conceptlist) {
			if(learner_data != nullptr) 
				mcmc_data.push_back(learner_data);
			
			learner_data = new MyHypothesis::data_t();
			decay_position = new std::vector<int>();
			learner_data->reserve(LEARNER_RESERVE_SIZE); // need to reserve enough here so that we don't have to move -- or else the pointers break		
			ndata = 0;
			decay_position_counter = 0;
			data_idx = 0;
		}
		
		// now put the relevant stuff onto learner_data
		for(size_t i=0;i<objs->size();i++) {
			assert(learner_data != nullptr and decay_position != nullptr);
			MyInput inp{objs->at(i), *objs};
			learner_data->emplace_back(inp, corrects->at(i), alpha);
			decay_position->push_back(decay_position_counter); // these all occur at the same decay position
			assert(learner_data->size() < LEARNER_RESERVE_SIZE);
		}

		// now unpack this data into human_data for each point in the concept
		for(size_t i=0;i<objs->size();i++) {
			// make a vector of responses
			std::vector<std::pair<bool,size_t>> v;
			v.push_back(std::make_pair(true,yeses->at(i)));
			v.push_back(std::make_pair(false,nos->at(i)));
			
			HumanDatum<MyHypothesis> hd{learner_data, 
										ndata, 
										&( learner_data->at(ndata+i).input ), 
										v, 
										0.5, 
										decay_position, 
										decay_position_counter};
			std::string key = conceptlist+"\t"+
								 str(data_idx)+"\t"+
								 str(*decay_position->rbegin())+"\t"+
								 str(corrects->at(i))+"\t"+
								 str(yeses->at(i))+"\t"+
								 str(nos->at(i));
								 
			// we will hold out list 2:
			if(contains(conceptlist,"L2")) {
				heldout_data_key.push_back(key);				
				heldout_data.push_back(std::move(hd));
				
			}
			else {
				human_data_key.push_back(key);
				human_data.push_back(std::move(hd));
			}
			
			data_idx++;
				
		}
		
		ndata += objs->size();
		decay_position_counter++; // this counts sets, not items in sets
		prev_conceptlist = conceptlist;	
	}
	if(learner_data != nullptr) 
		mcmc_data.push_back(learner_data); // and add that last dataset
	
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points." ENDL;
	COUT "# Loaded " << heldout_data.size() << " heldout data points." ENDL;
		
	if(runtype == "hypotheses" or runtype == "both") {
		
		auto h0 = MyHypothesis::sample(); 
		auto hypotheses = get_hypotheses_from_mcmc(h0, mcmc_data, InnerControl(), ntops);
		
		for(size_t t=0;t<ntops.size();t++) 
			save(hypothesis_path+"."+str(ntops[t]), hypotheses[t]);
	}

	
	if(runtype == "grammar" or runtype == "both") { 

		auto hypotheses = load<MyHypothesis>(hypothesis_path);
		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
		print("# Hypothesis size: ", hypotheses.size());
		
		auto h0 = MyGrammarHypothesis::sample(hypotheses, &human_data);
	
		TopN<MyGrammarHypothesis> top(1);
	
		// for computing the heldout probability
		// NOTE: This is slow for re-computing everything on the same hypotheses
		// but its probably fine for now
		print("# Initializing for computing heldout data");
		auto hhout = MyGrammarHypothesis::sample(hypotheses, &heldout_data);
	
		// Now actually run MCMC
		print("# Starting MCMC");
		if(FleetArgs::thin == 0) { print("# Warning: you have no thinning -- sampler will run much slower due to heldout computation");}
		auto thechain = MCMCChain(h0, &human_data);
		for(auto& h : thechain.run(Control()) | top | thin(FleetArgs::thin) ){
			std::string prefix = str(thechain.samples)+"\t";
			
			// evaluate the heldout score
			hhout.copy_parameters(h);
			print(prefix+"-1\theldout.ll\t"+str(hhout.compute_posterior(heldout_data)));
			
			print(h.string(prefix));
		}	
		
		

		// Now at the end let's output some model predictions along with the stored human_data_key 
		// we had from above, that gives us what data point we're talkign about
		{
			auto best = top.best();
			print("# Best training likelihood:", str(best.likelihood));
			auto hposterior = best.compute_normalized_posterior();
			std::ofstream out("output/training-best-predictions.txt");
			out << std::setprecision(32);
			for(size_t i=0;i<human_data.size();i++) {
				auto pr = best.compute_model_predictions(human_data, i, hposterior);
				out << human_data_key[i]+"\t" << best.alpha.get() << "\t" << get(pr,true,0.0) << "\t"+QQ( best.computeMAP(i,hposterior).string()) << "\n";			
			}
			out.close();
		}
		
		{
			hhout.copy_parameters(top.best());
			hhout.compute_posterior(heldout_data);
			auto hposterior = hhout.compute_normalized_posterior();
			std::ofstream out("output/heldout-best-predictions.txt");
			out << std::setprecision(32);
			for(size_t i=0;i<heldout_data.size();i++) {
				auto pr = hhout.compute_model_predictions(heldout_data, i, hposterior);
				out << heldout_data_key[i]+"\t" << hhout.alpha.get() << "\t" << get(pr,true,0.0)  << "\t"+QQ( hhout.computeMAP(i,hposterior).string()) << "\n";			
			}
			out.close();
			print("# Best heldout likelihood:", str(hhout.likelihood));
		}
		
	}
	
}