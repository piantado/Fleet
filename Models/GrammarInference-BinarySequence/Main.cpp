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
// Primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

double TERMINAL_P = 5.0;

std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : s.substr(1,S::npos)); }),
	Primitive("head(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : S(1,s.at(0))); }),
	Primitive("pair(%s,%s)",   +[](S& a, S b) -> void        { 
			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
			a.append(b); // modify on stack
	}), 
	
	Primitive("\u00D8",        +[]()         -> S          { return S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),

	Primitive("repeat(%s,%s)",    +[](S x, int y) -> S       { 
		S out = "";
		if(x.length()*y > MAX_LENGTH) throw VMSRuntimeError();
		for(int i=0;i<y;i++) 
			out += x;
		return out;			
	}),

	// swap O and B
	Primitive("invert(%s)",    +[](S x) -> S       { 
		S out(' ', x.size());
		for(auto& c : x) {
			if      (c == 'B') x.append("O");
			else if (c == 'O') x.append("B");
			else throw VMSRuntimeError();
		}
		return out;
	}),

	Primitive("1",    +[]() -> int { return 1; }),
	Primitive("2",    +[]() -> int { return 2; }),
	Primitive("3",    +[]() -> int { return 3; }),
	Primitive("4",    +[]() -> int { return 4; }),
	Primitive("5",    +[]() -> int { return 5; }),
	Primitive("6",    +[]() -> int { return 6; }),
	Primitive("7",    +[]() -> int { return 7; }),
	Primitive("8",    +[]() -> int { return 8; }),
	Primitive("9",    +[]() -> int { return 9; }),
	
	// for FlipP
	Primitive("0.1",    +[]() -> double { return 0.1; }),
	Primitive("0.2",    +[]() -> double { return 0.2; }),
	Primitive("0.3",    +[]() -> double { return 0.3; }),
	Primitive("0.4",    +[]() -> double { return 0.4; }),
	Primitive("0.5",    +[]() -> double { return 0.5; }),
	Primitive("0.6",    +[]() -> double { return 0.6; }),
	Primitive("0.7",    +[]() -> double { return 0.7; }),
	Primitive("0.8",    +[]() -> double { return 0.8; }),
	Primitive("0.9",    +[]() -> double { return 0.9; }),

	Primitive("length(%s)",   +[](S x) -> int { return x.length(); }),

	Primitive("cnt(%s,%s)",   +[](S x, S y) -> int { 
		return count(x,y);
	}),

	Primitive("'B'",       +[]()         -> S { return S("B"); }, TERMINAL_P),
	Primitive("'O'",       +[]()         -> S { return S("O"); }, TERMINAL_P),
	
	// And add built-ins - NOTE these must come last
	Builtin::And("and(%s,%s)"),
	Builtin::Or("or(%s,%s)"),
	Builtin::Not("not(%s)"),
	
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::FlipP("flip(%s)", 5.0),
	Builtin::Recurse<S,S>("F(%s)")	
};



///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Set up the grammar 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
using MyGrammar = Grammar<S,bool,int,double>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Define the kind of hypothesis we're dealing with
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const datum_t& x) override {	
		const auto out = call(x.input, "<err>"); 
		
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


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// I must declare my own in order to fill in the this_t in the template
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "GrammarHypothesis.h" 

/**
 * @class MyGrammarHypothesis
 * @author Steven Piantadosi
 * @date 11/09/20
 * @file Main.cpp
 * @brief Here is a version where we include inferring a parameter for runtime. I have to override a few things:
 */
class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis>;
	using Super::Super;


	virtual void recompute_P(std::vector<MyHypothesis>& hypotheses, const data_t& human_data) override {
		assert(which_data == std::addressof(human_data));

		// Because of the format of the data, we need to rewrite P to compute the conditional output
		// of B vs O, conditioning on some prefix sequence. We will predict from the empty string
		// but compute from the output of the hypothesis a conditional probability of B and O
		
		P.reset(new Predict_t(hypotheses.size(), human_data.size())); 
		
		#pragma omp parallel for
		for(size_t di=0;di<human_data.size();di++) {	
			for(size_t h=0;h<hypotheses.size();h++) {		
				const datum_t& hd = human_data[di];
				auto ret = hypotheses[h](*hd.predict);
				
				// get what the subject saw (as output) so we can marignalize
				S& seen = hd.data[0][0].output; assert(hd.ndata == 1); assert(hd.data[0].size() == 1);
				
				// now we need to compute the probability conditioned on the prefix
				double blp = -infinity; // just for this example
				double olp = -infinity; 
				for(auto& [s, lp] : ret.values() ) {
					// if it's a longer string containing seen
					// then it will contribute to the conditional probability
					if(s.length() > seen.length() and is_prefix(seen, s)) {
						
						if(s.at(seen.length()) == 'B') 
							blp = logplusexp(blp, lp);
						else if(s.at(seen.length()) == 'O') 
							olp = logplusexp(olp, lp);
						else
							assert(false);
					}
				}
				double z = logplusexp(blp, olp);				
				
				// now we make a vector of each "output" and their conditional
				// probabilities				
				std::vector<std::pair<S,double>> v;
				v.emplace_back("B", blp-z);
				v.emplace_back("O", olp-z);
				
				P->at(h,di) = v;
			}
		}
	}
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
	std::map<MyInput,std::map<S,size_t>> d; 
	std::ifstream fs("binary_data.csv");
	S line; std::getline(fs, line); // skip the first line
	while(std::getline(fs, line)) {
		auto parts = split(line, ',');
		auto [seen, generated] = std::tie(parts[4], parts[5]);
		
		// this input is sequential with each character of generated coming after each part of seen
		// and the output is a single character (in computeP of MyGrammarHypothesis)
		// So we're goign to pretend that the output data is a single character (the human's next prediction
		// before they see more data) and override recompute_P above to compute the marginal probability
		// NOTE This is being a little tricky and pretending the next output was just a string
		
		assert(seen.length() == generated.length());
		for(size_t i=0;i<seen.length();i++) {
			d[seen.substr(0,i)][S(1,generated.at(i))]++; // put a single character as this string
		}
	}
	
//	for(auto& v : d) {
//		CERR v.first TAB v.second["O"] TAB v.second["B"] ENDL;
//	}
		
	///////////////////////////////
	// Go through and convert this data to the form we need. 
	///////////////////////////////
	for(const auto& x : d) {		
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