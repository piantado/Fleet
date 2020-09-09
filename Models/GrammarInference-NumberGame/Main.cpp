
// We will define this so we can read from the NumberGame without main
#define NUMBER_GAME_DO_NOT_INCLUDE_MAIN 1

#include "../NumberGame/Main.cpp"
#include "Data/HumanDatum.h"

#include "Top.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"

std::string hypothesis_path = "hypotheses.txt";
std::string runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

// We're going to define a new kind of HumanDatum where the input is an int and the 
// output is a list of yes/no counts for each integer (stored in a vector)
// NOTE: This overrides the standard HumanDatum types since NumberGame naturally has a different format
using MyHumanDatum = HumanDatum<MyHypothesis, int, std::vector<std::pair<int,int>>>;

#include "GrammarHypothesis.h"

// Now define a GrammarHypothesis that uses this data type:
class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using data_t = Super::data_t;		
	
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
				if(hposterior(h,i) < 1e-6) continue; // these contribute very little...
				
				assert(P->at(h,i).size() == 1); // should be just one element
				assert(P->at(h,i)[0].second == 1.0); // should be 100% probability
				NumberSet& ns = P->at(h,i)[0].first; // this hypothesis' number set
				
				for(auto& n : ns) {
					ps[n] += hposterior(h,i);
				}
			}
			
			double ll = 0.0; // the likelihood here
			for(size_t n=Nlow;n<=N;n++) {
				auto p = ps[n];
				/// and the likelihood of yes and no
				ll += log( (1.0-alpha)*di.chance + alpha*p) * di.responses[n].first;
				ll += log( (1.0-alpha)*di.chance + alpha*(1.0-p)) * di.responses[n].second;
			}
			
			#pragma omp critical
			this->likelihood += ll;
		}
		
		return this->likelihood;		
	}
};

size_t grammar_callback_count = 0;
void gcallback(MyGrammarHypothesis& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT h.string(str(grammar_callback_count)+"\t");
	}
}


int main(int argc, char** argv){ 	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.initialize(argc, argv);
	
	MyGrammar grammar(PRIMITIVES);
	
	// don't forget to add these or the grammar won't finish
	for(int i=Nlow;i<=N;i++) {
		grammar.add<int>(BuiltinOp::op_INT, str(i), 10.0/N, i);		
	}
	
	// two main things we are building -- a list of human data points
	std::vector<MyHumanDatum> human_data;

	// and a list of MCMC data points
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	// an auxiliary data structure of sets and counts of yes/no for each number
	std::map<std::multiset<int>,std::vector<std::pair<int,int>>> d;
	
	///////////////////////////////
	// We'll read all the input here and dump it into a map
	///////////////////////////////	
	
	int* one = new int(1);
	
	std::ifstream fs("data.txt");
	std::string line; std::getline(fs, line); // skip the first line
	while(std::getline(fs, line)) {
		auto parts  = split(line, '\t');
		auto target = stoi(parts[0]);
		int yes     = stoi(parts[2]);
		int no      = stoi(parts[3]);
	
		// build up the multiset of data from this line
		std::multiset<int> s;
		for(const auto& x : split(parts[1], ',')) {
			s.insert(atoi(x.c_str()));
		}
		
		if(d.find(s) == d.end()) {
			d[s] = std::vector(N+1,std::make_pair(0,0));
		}
		
		
		// convert s into a NumberSet and insert (growing the vector if needed)
		d[s][target] = std::make_pair(yes,no);
	}
		
	///////////////////////////////
	// Go through and convert this data to the form we need. 
	///////////////////////////////
	
	for(const auto& k : d) {		
		// the learner sees 0 -> the set (first in values of d)
		// NOTE: We must put these on the heap so that they can be accessed outside of this loop
		auto learner_data = new MyHypothesis::data_t();
		
		// the data for NumberGame is actually a multiset (which is different than a NumberSet)
		learner_data->push_back(k.first);
		
		// store this as something we run MCMC on 
		mcmc_data.push_back(learner_data);
		
		// now the human data comes from d:
		// k.second already is a vector of yes/no pairs
		// Hmm 1/N is not really the right chance rate here -- its really the probability
		// for each individual number, but it's probably good for it to be sparse?
		MyHumanDatum hd{learner_data, learner_data->size(), one, k.second, 1.0/N, 0};
		human_data.push_back(std::move(hd));
	}
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	///////////////////////////////
	// Now handle main running 
	///////////////////////////////	
	
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
		
		// Set this up so that we don't do MCMC on some grammar rules
		size_t i = 0;
		for(auto& r : grammar) {
			// we'll not propose to all those goddamn integers we added
			// NOTE when we do this, GrammarHypothesis won't print these either
			if(r.instr.is_a(BuiltinOp::op_INT)) {
				h0.logA.set(i, 0.0); // set the value to 1 (you MUST do this) 
				h0.set_can_propose(i, false);
			}
			i++;
		}
		
		
		
		
		// tODO: HMM 
		//	where did the non-op-int rules go?
		
		
		
		
	
		tic();
		auto thechain = MCMCChain<MyGrammarHypothesis, decltype(gcallback)>(h0, &human_data, &gcallback);
		thechain.run(Control());
		tic();
	}
	
}