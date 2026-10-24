#define NUMBER_GAME_DO_NOT_INCLUDE_MAIN 1

#include "../NumberGame/Main.cpp"
#include "Data/HumanDatum.h"

#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"

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

const size_t A = 10; // alphabet, from zero 

std::string hypothesis_path = "hypotheses.txt";
std::string runtype = "both";


// responses to yes/no to each set
using Response_t {
	int yes;
	int no; 
};

// We're going to define a new kind of HumanDatum where the input is an int and the 
// response type is a lsit of yes/no counts for each integer (rather than yes/no counts for
// each possible NumberSet)
// NOTE: This overrides the standard HumanDatum types since NumberGame naturally has a different format
using MyHumanDatum = HumanDatum<MyHypothesis, 
								NumberSet, 
								Response_t>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the GrammarHypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "NewGrammarHypothesis.h"

// we are condition to condition on vectors instead of spans as the data
class MyGrammarHypothesis final : public NewGrammarHypothesis<MyGrammarHypothesis, 
															  MyHypothesis,
															  MyHumanDatum> {
public:
	using Super = NewGrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using this_t = MyGrammarHypothesis;
	
	
	virtual double compute_likelihood(const data_t& human_data, const double breakout=-infinity) override {
		// recompute our likelihood if its the wrong size or not using this data
		if(which_data != std::addressof(human_data)) { 
			CERR "*** You are computing likelihood on a dataset that the GrammarHypothesis was not constructed with." ENDL
			CERR "		You must call set_hypotheses_and_data before calling this likelihood, but note that when you" ENDL
			CERR "      do that, it will need to recompute everything (which might take a while)." ENDL;
			assert(false);
		}	
	
		Matrix hposterior = compute_normalized_posterior();
		
		this->likelihood  = 0.0; // of the human data
		
		#pragma omp parallel for
		for(size_t i=0;i<human_data.size();i++) {
			
			const auto& di = human_data[i];
			
			std::vector<double> ps(N+1,0); 
			for(size_t h=0;h<nhypotheses();h++){
				
				// these contribute very little and should be skipped
				if(hposterior(h,i) < 1e-6) continue; 
				
				NumberSet& ns = P->at(h,i); // this hypothesis' number set
				
				for(auto& n : ns) {
					ps[n] += hposterior(h,i);
				}
			}
			
			double ll = 0.0; // the likelihood here
			for(size_t n=Nlow;n<=N;n++) {
				auto p = ps[n];
				/// and the likelihood of yes and no
				ll += log( (1.0-alpha.get())*di.chance + alpha.get()*p) * di.responses[n].first;
				ll += log( (1.0-alpha.get())*di.chance + alpha.get()*(1.0-p)) * di.responses[n].second;
			}
			
			#pragma omp critical
			this->likelihood += ll;
		}
		
		return this->likelihood;		
	}
};




////////////////////////////////////////////////////////////////////////////////////////////

#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"

// For grammar inference:
#include "Generate.h"
#include "HumanDatum.h"

int main(int argc, char** argv){ 	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; mcmc = run grammar inference; both = do both");
	fleet.initialize(argc, argv);
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	print("# Loading human data");
	
	std::vector<HumanDatum<MyHypothesis,
						   std::vector<MyHypothesis::datum_t> 
						   >> human_data;
	
	std::ifstream infile("clean_learn_per_trial_expanded.csv"); //(FleetArgs::input_path);
	std::string line; 
	std::getline(infile, line); // skip header
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		auto& b = std::getline(infile, line);
		if(not b) break;
		
		auto[subjectno,cond1,cond2,cond_s,gen_s,res_s] = split<6>(line, '\t');
		
		auto cond = string_to<std::vector<MyHypothesis::datum_t>>(cond_s, ';');
		auto gen  = string_to<MyHypothesis::input_t>(gen_s);
		auto res  = string_to<MyHypothesis::output_t>(res_s);
		std::map<List,int> responses; responses[res] = 1; // 
		
		std::vector<float> decays;
		for(size_t i=0;i<cond.size();i++){
			decays.push_back(cond.size()-i); // so [a,b,c] goes to [3,2,1] so that the previous data point has *no* decay 
		}
				
		human_data.emplace_back(cond, decays, gen, responses);

	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	std::vector<MyHypothesis> hypotheses;
	if(runtype == "hypotheses" or runtype == "both"){	
		print("# Generating hypotheses");


		hypotheses = generate_from(human_data);

		print("# Using ", hypotheses.size(), " hypotheses.");
		

		save(hypothesis_path, hypotheses);
	}
	else {
		print("# Loading", hypothesis_path);
		hypotheses = load<MyHypothesis>(hypothesis_path);
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	if(runtype == "mcmc" or runtype == "both") {
			

		MyGrammarHypothesis gh0(hypotheses);
		
		// Set the constantIntegers not to be sampled
		for(auto& [r,ri] : *gh0.rule_indexer) {
			if(r->nt == grammar.nt<ConstantInteger>()) {
				gh0.logA.at(ri).can_propose = false; // don't propose to these
				gh0.logA.at(ri).value = 1.0; 
			}
		}	
		
		print("# Setting data.");

		// add our data to g0
		for(auto& d : human_data) {
			gh0.add_HumanData(d);
		}
		
		print("# Running sampler");
		
		size_t nsamples = 0;
		std::vector<NullDatum_t> Gdata;
		auto Gchain = MCMCChain(gh0, Gdata);
		for(auto& h : Gchain.run(Control()) | thin(FleetArgs::thin) ) {
			h.show(str(nsamples)+"\t");
			nsamples++;
		}
	}

	return 0;	
}
	
