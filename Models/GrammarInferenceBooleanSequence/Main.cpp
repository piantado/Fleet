

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
#include "Object.h"

typedef std::string S;
typedef S MyInput;
typedef S MyOutput;

const float strgamma = 0.01;
const size_t MAX_LENGTH = 64;
const auto log_A = log(2); // size of the alphabet -- here fixed in the grammar to 2 (not command line!)

// probability of right string by chance -- small but should be nonzero in case we don't find the string
// This could change by data point but we'll leave it constant here
const double pchance = 0.000000001; 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

double TERMINAL_P = 5.0;


std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : s.substr(1,S::npos)); }),
	Primitive("head(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : S(1,s.at(0))); }),
	// This version takes a reference for the first argument and that is assumed (by Fleet) to be the
	// return value. It is never popped off the stack and should just be modified. 
	Primitive("pair(%s,%s)",   +[](S& a, S b) -> void        { 
			if(a.length() + b.length() > MAX_LENGTH) 
				throw VMSRuntimeError;
			a.append(b); // modify on stack
	}), 
	
	Primitive("\u00D8",        +[]()         -> S          { return S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	
	Primitive("B",       +[]()         -> S { return S("B"); }, TERMINAL_P),
	Primitive("O",       +[]()         -> S { return S("O"); }, TERMINAL_P),
	
	// And add built-ins - NOTE these must come last
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::Flip("flip()", 10.0),
	Builtin::SafeRecurse<S,S>("F(%s)")	
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<S,bool> {
	using Super = Grammar<S,bool>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 

#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const datum_t& x) override {	
		const auto out = call(x.input, "<err>", this, 256, 256); 
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		double lp = -infinity;
		for(auto& o : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, o.second + p_delete_append<strgamma,strgamma>(o.first, x.output, log_A));
		}
		return lp;
	}
		
	void print(std::string prefix="") override {
		// we're going to make this print by showing the language we created on the line before
		prefix = prefix+"#\n#" +  this->call("", "<err>").string() + "\n";
		Super::print(prefix); 
	}
};

#include "GrammarHypothesis.h" 

size_t grammar_callback_count = 0;
void gcallback(GrammarHypothesis<MyHypothesis>& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT h.string(str(grammar_callback_count)+"\t");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
#include "Top.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"

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
	
	// two main things we are building
	std::vector<HumanDatum<MyHypothesis>> human_data;
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	S* const emptystr = new S("");
	
	///////////////////////////////
	// We'll read all the input here and dump it into a map
	///////////////////////////////	
	std::map<MyInput,std::map<MyOutput,size_t>> d; 
	std::ifstream fs("binary_data.csv");
	S line; std::getline(fs, line); // skip the first line
	while(std::getline(fs, line)) {
		auto parts = split(line, ',');
		auto [input, output] = std::tie(parts[4], parts[5]);
		d[input][output]++;  // just add up the input/output pairs we get
		//CERR input TAB output TAB d[input][output] ENDL;
	}
		
	///////////////////////////////
	// Go through and convert this data to the form we need. 
	///////////////////////////////
	for(const auto& x : d) {		
		// the learner sees "" -> x.first
		auto learner_data = new MyHypothesis::data_t();
		learner_data->emplace_back("", x.first, 0.0);
		
		// store this as something we run MCMC on 
		mcmc_data.push_back(learner_data);
		
		// now the human data comes from d:
		HumanDatum<MyHypothesis> hd{learner_data, learner_data->size(), emptystr, x.second, pchance, 0};
		human_data.push_back(std::move(hd));
	}
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	///////////////////////////////
	// Now handle main running 
	///////////////////////////////	
	
	std::vector<MyHypothesis> hypotheses; 
	if(runtype == "hypotheses" or runtype == "both") {
		
		auto h0 = MyHypothesis::make(&grammar); 
		hypotheses = get_hypotheses_from_mcmc(h0, mcmc_data, Control(inner_mcmc_steps, inner_runtime), ntop);
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
		
		auto h0 = GrammarHypothesis<MyHypothesis>::make(hypotheses, &human_data);
	
		tic();
		auto thechain = MCMCChain(h0, &human_data, &gcallback);
		thechain.run(Control(mcmc_steps, runtime));
		tic();
	}
	
}