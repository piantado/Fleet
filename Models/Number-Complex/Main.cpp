//  TODO: Update ANS= to be something reasonable!
//			- ANSeq needs to normalize by the integers, right?

// TODO: Add set operations for wmsets?



//#define PARALLEL_TEMPERING_SHOW_DETAIL
//#define DEBUG_CHAINPOOL

#include <vector>
#include <string>
#include <random>

double recursion_penalty = -75.0;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables for the model
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef int         word;
typedef std::string set; 	/* This models sets as strings */
typedef char        objectkind; // store this as that
typedef short       wmset; // just need an integerish type
typedef float       magnitude; 

struct utterance { set s; objectkind o; }; 

std::vector<objectkind>   OBJECTS = {'a', 'b', 'c', 'd', 'e'}; //, 'f', 'g', 'h', 'i', 'j'};
std::vector<std::string>    WORDS = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
std::vector<magnitude> MAGNITUDES = {1,2,3,4,5,6,7,8,9,10};

const word U = -999;

const size_t MAX_SET_SIZE = 25;
const double alpha = 0.9;
const double W = 0.2; // weber ratio for ans

std::discrete_distribution<> number_distribution({0, 7187, 1484, 593, 334, 297, 165, 151, 86, 105, 112}); // 0-indexed
	
std::vector<int> data_amounts = {1, 2, 3, 4, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 125, 150, 200, 250, 300, 350, 400, 500, 600, 1000};//, 500, 600, 700, 800, 900, 1000};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Random.h"

double ANSzero(const double n1, const double n2) {
	// probability mass assigned to exactly 0 under weber scaling
	// TODO: May want to make this be between -0.5-0.5? 
	return exp(normal_lpdf(0.0, n1-n2, W*sqrt(n1*n1+n2*n2+1))); // NOTE: The +1 here because we need to be able to compare zeros!
}
double normcdf(const double x) { 
	return 0.5 * (1.0 + erf(x * M_SQRT1_2));
}

double ANSdiff(const double n1, const double n2) {  // SD of the weber value
	if(n1 == 0.0 && n2 == 0.0) return 0.0; // hmm is this right?
	
	return (n1-n2) / (W*sqrt(n1*n1+n2*n2));
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<utterance,word,      bool,word,set,objectkind,utterance,wmset,magnitude,double>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
			
		add("undef",         +[]() -> word { return U; });
		
		add("a",         +[]() -> objectkind { return 'a'; });
		add("b",         +[]() -> objectkind { return 'b'; });
		add("c",         +[]() -> objectkind { return 'c'; });
		add("d",         +[]() -> objectkind { return 'd'; });
		add("e",         +[]() -> objectkind { return 'e'; });
		
		add("one",         +[]() -> word { return 1; }, 0.1);
		add("two",         +[]() -> word { return 2; }, 0.1);
		add("three",       +[]() -> word { return 3; }, 0.1);
		add("four",        +[]() -> word { return 4; }, 0.1);
		add("five",        +[]() -> word { return 5; }, 0.1);
		add("six",         +[]() -> word { return 6; }, 0.1);
		add("seven",       +[]() -> word { return 7; }, 0.1);
		add("eight",       +[]() -> word { return 8; }, 0.1);
		add("nine",        +[]() -> word { return 9; }, 0.1);
		add("ten",         +[]() -> word { return 10; }, 0.1);
		
		add("next(%s)",    +[](word w) -> word { return w == U ? U : w+1;});
		add("prev(%s)",    +[](word w) -> word { return w == U or w == 1 ? U : w-1; });
		
		// extract from the context/utterance
		add("%s.set",      +[](utterance u) -> set    { return u.s; }, 25.0);
		add("%s.obj",      +[](utterance u) -> objectkind { return u.o; }, 10.0);
		
		// build utterances -- needed for useful recursion
		add("<%s,%s>",     +[](set s, objectkind o) -> utterance { return utterance{s,o}; });
		
		add("union(%s,%s)", +[](set x, set y) -> set { 
			if(x.size()+y.size() > MAX_SET_SIZE) 
				throw VMSRuntimeError(); 
			return x+y; 
		});
		
		add("intersection(%s,%s)", +[](set x, set y) -> set {
			std::string out = "";
			for(size_t yi=0;yi<y.length();yi++) {
				std::string s = y.substr(yi,1);
				size_t pos = x.find(s); // do I occur?
				if(pos != std::string::npos) {
					out.append(s);
				}
			}
			return out;
		});
		
		add("difference(%s,%s)", +[](set x, set y) -> set {
			std::string out = x;
			for(size_t yi=0;yi<y.length();yi++) {
				std::string s = y.substr(yi,1);
				size_t pos = out.find(s); // do I occur?
				if(pos != std::string::npos) {
					out.erase(pos,1); // remove that character
				}
			}
			return out;
		});
		
		add("filter(%s,%s)", +[](set x, objectkind t) -> set {
			std::string tstr = std::string(1,t); // a std::string to find
			std::string out = "";
			size_t pos = x.find(tstr);
			while (pos != std::string::npos) {
				out = out + tstr;
				pos = x.find(tstr,pos+1);
			}
			return out;
		});
		
		add("select(%s)",      +[](set x) -> set { 
			return x.substr(0,1); 
		});
		
		add("selectO(%s,%s)",  +[](set x, objectkind o) -> set {
			if(x.find(o) != std::string::npos){
				return std::string(1,o); // must exist there
			}
			else {
				return set("");
			}
		});
		
		add("match(%s,%s)",  +[](wmset x, set s) -> bool { return x == (wmset)s.size(); }, 1.0);
		add("{}",            +[]() -> wmset { return (wmset)0; });
		add("{o}",           +[]() -> wmset { return (wmset)1; });
		add("{o,o}",         +[]() -> wmset { return (wmset)2; });
		add("{o,o,o}",       +[]() -> wmset { return (wmset)3; });
			
		// ANS operations 
		add("ANSeq(%s,%s)",     +[](set a, set b)       -> double { return ANSzero(a.length(), b.length()); }, 1.0/3.0);
		add("ANSeq(%s,%s)",     +[](set a, wmset b)     -> double { return ANSzero(a.length(), b); }, 1.0/3.0);
		add("ANSeq(%s,%s)",     +[](set a, magnitude b) -> double { return ANSzero(a.length(), b); }, 1.0/3.0);
		
		add("ANSlt(%s,%s)",     +[](set a, set b)       -> double { return 1.0-normcdf(ANSdiff(a.length(), b.length())); }, 1.0/5.0);
		add("ANSlt(%s,%s)",     +[](set a, wmset b)     -> double { return 1.0-normcdf(ANSdiff(a.length(), b)); }, 1.0/5.0);
		add("ANSlt(%s,%s)",     +[](set a, magnitude b) -> double { return 1.0-normcdf(ANSdiff(a.length(), b)); }, 1.0/5.0);
		add("ANSlt(%s,%s)",     +[](wmset b, set a)     -> double { return 1.0-normcdf(ANSdiff(b, a.length())); }, 1.0/5.0);
		add("ANSlt(%s,%s)",     +[](magnitude b, set a) -> double { return 1.0-normcdf(ANSdiff(b, a.length())); }, 1.0/5.0);

		add("1",     +[]() -> magnitude { return 1; });
		add("2",     +[]() -> magnitude { return 2; });
		add("3",     +[]() -> magnitude { return 3; });
		add("4",     +[]() -> magnitude { return 4; });
		add("5",     +[]() -> magnitude { return 5; });
		add("6",     +[]() -> magnitude { return 6; });
		add("7",     +[]() -> magnitude { return 7; });
		add("8",     +[]() -> magnitude { return 8; });
		add("9",     +[]() -> magnitude { return 9; });
		add("10",    +[]() -> magnitude { return 10; });
		
		add("and(%s,%s)",    Builtins::And<MyGrammar>, 1./3);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>, 1./3);
		add("not(%s)",       Builtins::Not<MyGrammar>, 1./3);
		
		add("x",             Builtins::X<MyGrammar>, 10.);
		add("flip()",        Builtins::Flip<MyGrammar>, 2.0);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,set>,        1./5);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,word>,       1./5);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,wmset>,      1./5);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,objectkind>, 1./5);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,magnitude>,  1./5);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,utterance>,  1./5);
		add("recurse(%s)",   Builtins::Recurse<MyGrammar>);
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,utterance,word,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,utterance,word,MyGrammar,&grammar>;
	using Super::Super;
	
	double compute_prior() override {
		// include recusion penalty
		return prior = Super::compute_prior() +
		       (recursion_count() > 0 ? recursion_penalty : log1p(-exp(recursion_penalty))); 
	}
	
	DiscreteDistribution<word> call(const utterance& input) {
		return Super::call(input, U);
	}
	
	double compute_single_likelihood(const datum_t& d) override {
		
		auto v = call(d.input); // something of the type
		
		// average likelihood when sampling from this posterior
		return log( exp(v.lp(U)) * (1.0/10.0) + (1.0-d.reliability)/10.0 + d.reliability * exp(v.lp(d.output)) );
	}	

	
	virtual void print(std::string prefix="") override {
		
		// Hmm let's run over all the data and just get the modal response
		// for each data output from the training data set 
		std::map<word,DiscreteDistribution<word>> p; // probability of W given target
		extern std::vector<data_t> alldata;
		for(auto& di : alldata[alldata.size()-1]) {
			// count how many times the object occurs
			// NOTE: We cannot use di.output here since that's generated with noise
			const word target  = count(di.input.s, std::string(1,di.input.o)); 
			
			auto v = call(di.input); // something of the type

			for(const auto& o : v.values()) {
				p[target].addmass(o.first, o.second);
				
			}
		}

		std::string outputstring;
		for(const auto& x : p) {
			const word m = x.second.argmax();
			outputstring += (m == U ? "U" : str(m)) + ".";
		}
		
		prefix += QQ(outputstring)+"\t"+std::to_string(this->recursion_count())+"\t";
		Super::print(prefix);
		
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare global set of data
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "TopN.h"
TopN<MyHypothesis> all(TopN<MyHypothesis>::MAX_N); // used by MCMC and MCTS locally

std::vector<MyHypothesis::data_t> alldata;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// If we want to do MCTS
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//#include "Inference/MCTS.h"
//#include "Inference/MCMCChain.h"
//
//class MyMCTS final : public PartialMCTSNode<MyMCTS, MyHypothesis, decltype(all)> {
//	using PartialMCTSNode::PartialMCTSNode;
//
//	virtual void playout(MyHypothesis& h) override {
//
//		MyHypothesis h0 = h; // need a copy to change resampling on 
//		for(auto& n : h0.get_value() ){
//			n.can_resample = false;
//		}
//		
//		// make a wrapper to add samples
//		std::function<void(MyHypothesis& h)> cb = [&](MyHypothesis& v) { 
//			this->add_sample(v.likelihood);
//			(*this->callback)(v);
//		};
//		
//		h0.complete(); // fill in any structural gaps
//		MCMCChain chain(h0, data, cb);
//		chain.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1)); // run mcmc with restarts; we sure shouldn't run more than runtime
//	}
//	
//};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Sampling for the data
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

MyHypothesis::datum_t sample_datum() { 
	
	set s = ""; word w = U; objectkind t{};
	int ntypes=0;
	int NT = myrandom(1,4); // how many times do I try to add types?
	
	for(int i=0;i<NT;i++) {
		int nx = number_distribution(rng);  // sample how many things we'll do
		objectkind tx;
		do { 
			tx = OBJECTS[myrandom<size_t>(0,OBJECTS.size())];
		} while(s.find(std::string(1,tx)) != std::string::npos); // keep trying until we find an unused one
		
		s.append(std::string(nx,tx));
		
		if(i==0) { // first time captures the intended type
			if(uniform() < 1.0-alpha) w = myrandom(1,11);  // are we noise?
			else       				  w = nx; // not noise
			t = tx; // the type is never considered to be noise here
		}				
		ntypes++;     
	}
	std::random_shuffle( s.begin(), s.end() ); // randomize the order so select is not a friendly strategy
	
	return {utterance{s,t}, w, alpha};
}

MyHypothesis::datum_t sample_adjusted_datum(std::function<double(MyHypothesis::datum_t&)>& f) {
	// use some rejection sampling to adjust our samplign distribution via f
	// here, f gives the probability of accepting each word so that we can
	// upweight certain kinds of data. 
	do {
		auto d = sample_datum();
		if(uniform() < f(d)) {
			return d;
		}
	} while(true);
	
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Main
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "ParallelTempering.h"
#include "Fleet.h"


int main(int argc, char** argv) { 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Fancy number inference model");
	fleet.initialize(argc, argv);

	// can just set these
	VirtualMachineControl::MAX_STEPS = 128;
	VirtualMachineControl::MAX_OUTPUTS = 128;
	
	typedef MyHypothesis::data_t  data_t;
	typedef MyHypothesis::datum_t datum_t;
	
	// Set up the data -- we'll do this so we can run a parallel
	// chain across all of it at once
	std::vector<TopN<MyHypothesis>>   alltops;
	for(auto ndata : data_amounts) {
		data_t mydata;
		
		// make some data here
		for(int di=0;di<ndata;di++) {
			// make the data point
			mydata.push_back(sample_datum());
		}

		alldata.push_back(mydata);
		alltops.push_back(TopN<MyHypothesis>());
	}
	data_t biggestData = *alldata.rbegin();
	
	
	// Run parallel tempering
//	auto h0 = MMyHypothesis::sample();
//	ParallelTempering samp(h0, alldata, alltops);
//	tic();
//	samp.run(Control(mcmc_steps, runtime, nthreads), 200, 5000); 
//	tic();

	// just simple mcmc on one
	for(size_t di=0;di<alldata.size();di++){
		auto h0 = MyHypothesis::sample();
		MCMCChain samp(h0, &alldata[di]);
		for(auto& h : samp.run(Control())) {
			alltops[di] << h;
		}
		
	}
	
	fleet.completed(); // print the search/mcmc stats here instead of when fleet is destroyed
	
	// and save what we found
	for(auto& tn : alltops) {
		for(auto h : tn.values()) {
			h.clear_bayes(); // zero the prior, posterior, likelihood
			all << h;
		}
	}
		
	COUT "# Computing posterior on all final values |D|=" << biggestData.size()  ENDL;

	// print out at the end
	all.compute_posterior(biggestData).print("normal\t");
	
//	COUT "# Computing posterior for extra data <= 4" ENDL;
//	{
//		std::function<double(datum_t&)> f = [](datum_t d) {
//			return (d.output <= (word)4 ? 1.0 : 0.25);			
//		};
//		
//		// make some data
//		data_t newdata;
//		for(size_t i=0;i<biggestData.size();i++) {
//			auto di = sample_adjusted_datum(f);
//			//cnt[di.output]++;
//			newdata.push_back(di);
//		}
//		
//		all.compute_posterior(newdata).print("lt4\t");
//	}	
//	
//	COUT "# Computing posterior for extra data > 4" ENDL;
//	{
//		std::function<double(datum_t&)> f = [](datum_t d) {
//			return (d.output > (word)4 ? 1.0 : 0.25);			
//		};
//		
//		// make some data
//		data_t newdata;
//		for(size_t i=0;i<biggestData.size();i++) {
//			auto di = sample_adjusted_datum(f);
//			//cnt[di.output]++;
//			newdata.push_back(di);
//		}
//		
//		all.compute_posterior(newdata).print("gt4\t");
//	}	
//	
	
	

}

