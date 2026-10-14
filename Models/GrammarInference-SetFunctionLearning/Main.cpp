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

const size_t MAX_TRIALS = 5; // don't go more than this many into the trials, since that's mostly posterior--not priors

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Set up primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Define this so we don't import its main function
#define NO_FOL_MAIN 1

#include "../FirstOrderLogic/Main.cpp"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// I must declare my own in order to fill in the this_t in the template
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "HumanDatum.h"

// we store responess as yes/no vectors here
struct HumanResponse {
	std::vector<int> yes_responses;
	std::vector<int> no_responses;
};

using MyHumanDatum = HumanDatum<MyHypothesis,
							  std::vector<MyHypothesis::datum_t>,
							  HumanResponse>;

#include "NewGrammarHypothesis.h" 

class MyGrammarHypothesis final : public NewGrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum> {
public:
	using Super = NewGrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using this_t = MyGrammarHypothesis;
	
	
	virtual double compute_likelihood() override {
		
		// each hypothesis's prior
		const auto priors = this->hypothesis_prior(); // determined by counts and parameters
		const auto H = hypotheses->size();
		
		this->likelihood = 0.0; // easier to just compute here
		
		#pragma omp parallel for
		for(size_t di=0;di<human_data->size();di++){
			auto& hd = human_data->at(di);
			auto n = hd.human_responses.yes_responses.size();
			
			// get each data point's marginal over responses
			Vector posteriors(H);
			for(size_t hi=0;hi<H;hi++) {
				posteriors(hi) =  priors(hi) + 
				                  hd.likelihood_with_decay(hi, decay) / double(people_LLT); // likelihood temperatures here
			}
			
			// Normalized posteriors on this data
			const Vector lpost = lognormalize(posteriors).array();
			
			// for this we need to make a marginal distribution on true/false for
			// each set element
			std::vector<double> marginal_yes(n, -infinity);
			for(size_t hi=0;hi<H;hi++) {
				for(size_t ri=0;ri<n;ri++) {
					if(hd.hypotheses_responses.at(hi).at(ri))
						marginal_yes.at(ri) = logplusexp<double>(marginal_yes.at(ri), lpost(hi));
				}
			}
		
			//print(str(marginal_yes), str(hd.human_responses.yes_responses), str(hd.human_responses.no_responses), log1m(marginal_yes.at(0)));
		
			// now compute on the humna data
			double myll = 0.0;
			for(size_t ri=0;ri<n;ri++) {
				myll += hd.human_responses.yes_responses.at(ri) * marginal_yes.at(ri);
				myll += hd.human_responses.no_responses.at(ri)  * log1m(marginal_yes.at(ri));
				
			}
		
			// TODO: Do not do this in parallel -- because we are tracking bins etc
			#pragma omp critical
			{
				this->likelihood += myll;
			}
			
		}
		
		return this->likelihood; 
	}
	
	
};


							  
////////////////////////////////////////////////////////////////////////////////////////////
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"
#include "Vectors.h"
#include "Generate.h"

const double alpha = 0.75; // in learning

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
	
	print("# Loading human data");
	
	std::vector<MyHumanDatum> human_data;
	
	std::ifstream infile("human-data.txt"); //(FleetArgs::input_path);
	std::string line; 
	std::getline(infile, line); // skip header
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		auto& b = std::getline(infile, line);
		if(not b) break;
		print(line);
		
		auto [conc, list, setnumber, cond_s, gen_s, yesv, nov] = split<7>(line, '\t');
		
		auto cond = str_to<MyHumanDatum::ConditionOn_t>(cond_s, ';');
		
		if(cond.size() < MAX_TRIALS) {
			
			// set all the alphas
			for(auto& di : cond) {
				di.reliability = alpha;
			}
			
			auto gen  = str_to<MyHypothesis::input_t>(gen_s);
			
			auto y = str_to<std::vector<int>>(yesv);
			auto n = str_to<std::vector<int>>(nov);
			HumanResponse hr{y,n};
			
			assert(gen.size() == y.size());
			assert(gen.size() == n.size());

			std::vector<float> decays;
			for(size_t i=0;i<cond.size();i++){
				decays.push_back(cond.size()-i); // so [a,b,c] goes to [3,2,1] so that the previous data point has *no* decay 
			}
					
			human_data.emplace_back(cond, decays, gen, hr);
			print(str(cond), str(gen), str(y), str(n));
		}
			
		
		//print(*human_data.rbegin());
	}
	
	COUT "# Loaded " << human_data.size() << " human data points." ENDL;
	std::vector<MyHypothesis> hypotheses;
	if(runtype == "hypotheses" or runtype == "both") {
		hypotheses = generate_from<MyHypothesis,MyHumanDatum>(human_data);
		
		print("# Using ", hypotheses.size(), " hypotheses.");
		
		save(hypothesis_path, hypotheses);
	}
	else {
		print("# Loading", hypothesis_path);
		hypotheses = load<MyHypothesis>(hypothesis_path);
	}
	
	
	if(runtype == "mcmc" or runtype == "both") {
			
		MyGrammarHypothesis gh0(hypotheses);

		print("# Setting data.");

		// add our data to g0

		human_data = slice(human_data,0,250);
		gh0.add_HumanData(human_data);
		
		print("# Running sampler");
		
		size_t nsamples = 0;
		std::vector<NullDatum_t> Gdata;
		auto Gchain = MCMCChain(gh0, Gdata);
		for(auto& h : Gchain.run(Control()) | thin(FleetArgs::thin) ) {
			h.show(str(nsamples)+"\t");
			nsamples++;
		}
	}
		
//	
//	if(runtype == "grammar" or runtype == "both") { 
//
//		auto hypotheses = load<MyHypothesis>(hypothesis_path);
//		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
//		print("# Hypothesis size: ", hypotheses.size());
//		
//		auto h0 = MyGrammarHypothesis::sample(hypotheses, &human_data);
//	
//		TopN<MyGrammarHypothesis> top(1);
//	
//		// for computing the heldout probability
//		// NOTE: This is slow for re-computing everything on the same hypotheses
//		// but its probably fine for now
//		print("# Initializing for computing heldout data");
//		auto hhout = MyGrammarHypothesis::sample(hypotheses, &heldout_data);
//	
//		// Now actually run MCMC
//		print("# Starting MCMC");
//		if(FleetArgs::thin == 0) { print("# Warning: you have no thinning -- sampler will run much slower due to heldout computation");}
//		auto thechain = MCMCChain(h0, &human_data);
//		for(auto& h : thechain.run(Control()) | top | thin(FleetArgs::thin) ){
//			std::string prefix = str(thechain.samples)+"\t";
//			
//			// evaluate the heldout score
//			hhout.copy_parameters(h);
//			print(prefix+"-1\theldout.ll\t"+str(hhout.compute_posterior(heldout_data)));
//			
//			print(h.string(prefix));
//		}	
//		
//		
//
//		// Now at the end let's output some model predictions along with the stored human_data_key 
//		// we had from above, that gives us what data point we're talkign about
//		{
//			auto best = top.best();
//			print("# Best training likelihood:", str(best.likelihood));
//			auto hposterior = best.compute_normalized_posterior();
//			std::ofstream out("output/training-best-predictions.txt");
//			out << std::setprecision(32);
//			for(size_t i=0;i<human_data.size();i++) {
//				auto pr = best.compute_model_predictions(human_data, i, hposterior);
//				out << human_data_key[i]+"\t" << best.alpha.get() << "\t" << get(pr,true,0.0) << "\t"+QQ( best.computeMAP(i,hposterior).string()) << "\n";			
//			}
//			out.close();
//		}
//		
//		{
//			hhout.copy_parameters(top.best());
//			hhout.compute_posterior(heldout_data);
//			auto hposterior = hhout.compute_normalized_posterior();
//			std::ofstream out("output/heldout-best-predictions.txt");
//			out << std::setprecision(32);
//			for(size_t i=0;i<heldout_data.size();i++) {
//				auto pr = hhout.compute_model_predictions(heldout_data, i, hposterior);
//				out << heldout_data_key[i]+"\t" << hhout.alpha.get() << "\t" << get(pr,true,0.0)  << "\t"+QQ( hhout.computeMAP(i,hposterior).string()) << "\n";			
//			}
//			out.close();
//			print("# Best heldout likelihood:", str(hhout.likelihood));
//		}
//		
//	}
	
}