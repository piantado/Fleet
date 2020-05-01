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
typedef struct { set s; objectkind o; } utterance; 
typedef short       wmset; // just need an integerish type
typedef float       magnitude; 

std::vector<objectkind>   OBJECTS = {'a', 'b', 'c', 'd', 'e'}; //, 'f', 'g', 'h', 'i', 'j'};
std::vector<std::string>    WORDS = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
std::vector<magnitude> MAGNITUDES = {1,2,3,4,5,6,7,8,9,10};

const word U = -999;

const size_t MAX_SET_SIZE = 25;
const double alpha = 0.9;
const double W = 0.2; // weber ratio for ans

// TODO: UPDATE WITH data from Gunderson & Levine?
std::discrete_distribution<> number_distribution({0, 7187, 1484, 593, 334, 297, 165, 151, 86, 105, 112}); // 0-indexed
	
std::vector<int> data_amounts = {1, 2, 3, 4, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 125, 150, 200, 250, 300, 350, 400, 500, 600, 1000};//, 500, 600, 700, 800, 900, 1000};
//std::vector<int> data_amounts = {1000};
//std::vector<int> data_amounts = {1, 5, 10, 50, 100};//, 500, 600, 700, 800, 900, 1000};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define FLEET_GRAMMAR_TYPES bool,word,set,objectkind,utterance,wmset,magnitude,double

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the primitives (which are already defined in MyPrimitives
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

#include "Primitives.h"
#include "Builtins.h"	
	
std::tuple PRIMITIVES = {
	Primitive("undef",         +[]() -> word { return U; }),
	
	Primitive("a",         +[]() -> objectkind { return 'a'; }),
	Primitive("b",         +[]() -> objectkind { return 'b'; }),
	Primitive("c",         +[]() -> objectkind { return 'c'; }),
	Primitive("d",         +[]() -> objectkind { return 'd'; }),
	Primitive("e",         +[]() -> objectkind { return 'e'; }), // TODO: Older versions had through j 
	
	Primitive("one",         +[]() -> word { return 1; }, 0.1),
	Primitive("two",         +[]() -> word { return 2; }, 0.1),
	Primitive("three",       +[]() -> word { return 3; }, 0.1),
	Primitive("four",        +[]() -> word { return 4; }, 0.1),
	Primitive("five",        +[]() -> word { return 5; }, 0.1),
	Primitive("six",         +[]() -> word { return 6; }, 0.1),
	Primitive("seven",       +[]() -> word { return 7; }, 0.1),
	Primitive("eight",       +[]() -> word { return 8; }, 0.1),
	Primitive("nine",        +[]() -> word { return 9; }, 0.1),
	Primitive("ten",         +[]() -> word { return 10; }, 0.1),
	
	Primitive("next(%s)",    +[](word w) -> word { return w == U ? U : w+1;}),
	Primitive("prev(%s)",    +[](word w) -> word { return w == U or w == 1 ? U : w-1; }),
	
	// extract from the context/utterance
	Primitive("%s.set",      +[](utterance u) -> set    { return u.s; }, 25.0),
	Primitive("%s.obj",      +[](utterance u) -> objectkind { return u.o; }, 10.0),
	
	// build utterances -- needed for useful recursion
	Primitive("<%s,%s>",     +[](set s, objectkind o) -> utterance { return utterance{s,o}; }),
	
	Primitive("union(%s,%s)", +[](set x, set y) -> set { 
		if(x.size()+y.size() > MAX_SET_SIZE) 
			throw VMSRuntimeError; 
		return x+y; 
	}),
	Primitive("intersection(%s,%s)", +[](set x, set y) -> set {
		std::string out = "";
		for(size_t yi=0;yi<y.length();yi++) {
			std::string s = y.substr(yi,1);
			size_t pos = x.find(s); // do I occur?
			if(pos != std::string::npos) {
				out.append(s);
			}
		}
		return out;
	}),
	Primitive("difference(%s,%s)", +[](set x, set y) -> set {
		std::string out = x;
		for(size_t yi=0;yi<y.length();yi++) {
			std::string s = y.substr(yi,1);
			size_t pos = out.find(s); // do I occur?
			if(pos != std::string::npos) {
				out.erase(pos,1); // remove that character
			}
		}
		return out;
	}),
	Primitive("filter(%s,%s)", +[](set x, objectkind t) -> set {
		std::string tstr = std::string(1,t); // a std::string to find
		std::string out = "";
		size_t pos = x.find(tstr);
		while (pos != std::string::npos) {
			out = out + tstr;
			pos = x.find(tstr,pos+1);
		}
		return out;
	}),
	Primitive("select(%s)",      +[](set x) -> set { 
		return x.substr(0,1); 
	}),
	Primitive("selectO(%s,%s)",  +[](set x, objectkind o) -> set {
		if(x.find(o) != std::string::npos){
			return std::string(1,o); // must exist there
		}
		else {
			return set("");
		}
	}),
	Primitive("match(%s,%s)",  +[](wmset x, set s) -> bool { return x == (wmset)s.size(); }, 1.0),
	Primitive("{}",            +[]() -> wmset { return (wmset)0; }),
	Primitive("{o}",           +[]() -> wmset { return (wmset)1; }),
	Primitive("{o,o}",         +[]() -> wmset { return (wmset)2; }),
	Primitive("{o,o,o}",       +[]() -> wmset { return (wmset)3; }),
	
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }, 1.0/3.0),
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); },  1.0/3.0),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); },   1.0/3.0),
	
	// ANS operations 
	Primitive("ANSeq(%s,%s)",     +[](set a, set b)       -> double { return ANSzero(a.length(), b.length()); }, 1.0/3.0),
	Primitive("ANSeq(%s,%s)",     +[](set a, wmset b)     -> double { return ANSzero(a.length(), b); }, 1.0/3.0),
	Primitive("ANSeq(%s,%s)",     +[](set a, magnitude b) -> double { return ANSzero(a.length(), b); }, 1.0/3.0),
	
	Primitive("ANSlt(%s,%s)",     +[](set a, set b)       -> double { return 1.0-normcdf(ANSdiff(a.length(), b.length())); }, 1.0/5.0),
	Primitive("ANSlt(%s,%s)",     +[](set a, wmset b)     -> double { return 1.0-normcdf(ANSdiff(a.length(), b)); }, 1.0/5.0),
	Primitive("ANSlt(%s,%s)",     +[](set a, magnitude b) -> double { return 1.0-normcdf(ANSdiff(a.length(), b)); }, 1.0/5.0),
	Primitive("ANSlt(%s,%s)",     +[](wmset b, set a)     -> double { return 1.0-normcdf(ANSdiff(b, a.length())); }, 1.0/5.0),
	Primitive("ANSlt(%s,%s)",     +[](magnitude b, set a) -> double { return 1.0-normcdf(ANSdiff(b, a.length())); }, 1.0/5.0),

	Primitive("1",     +[]() -> magnitude { return 1; }),
	Primitive("2",     +[]() -> magnitude { return 2; }),
	Primitive("3",     +[]() -> magnitude { return 3; }),
	Primitive("4",     +[]() -> magnitude { return 4; }),
	Primitive("5",     +[]() -> magnitude { return 5; }),
	Primitive("6",     +[]() -> magnitude { return 6; }),
	Primitive("7",     +[]() -> magnitude { return 7; }),
	Primitive("8",     +[]() -> magnitude { return 8; }),
	Primitive("9",     +[]() -> magnitude { return 9; }),
	Primitive("10",    +[]() -> magnitude { return 10; }),
	
	//x, recurse, ifset, ifword
	Builtin::If<set>("if(%s,%s,%s)", 1/3.),		
	Builtin::If<word>("if(%s,%s,%s)", 1/3.),	
	Builtin::If<wmset>("if(%s,%s,%s)", 1./3.),		
	Builtin::If<objectkind>("if(%s,%s,%s)"),
	Builtin::If<magnitude>("if(%s,%s,%s)"),		
	Builtin::X<utterance>("x", 10.0),
	Builtin::FlipP("flip(%s)", 2.0),
	Builtin::Recurse<word,utterance>("F(%s)")	
};

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

class MyHypothesis : public LOTHypothesis<MyHypothesis,utterance,word> {
public:
	using Super = LOTHypothesis<MyHypothesis,utterance,word>;
	using Super::Super;
	
	size_t recursion_count() {
		size_t cnt = 0;
		for(auto& n : value) {
			cnt += n.rule->instr.is_a(BuiltinOp::op_RECURSE);
		}
		return cnt;
	}
		
	double compute_prior() override {
		// include recusion penalty
		prior = Super::compute_prior() +
		       (recursion_count() > 0 ? recursion_penalty : log(1.0-exp(recursion_penalty))); 
		
		return prior;
	}
	
	DiscreteDistribution<word> call(const utterance& input) {
		return Super::call(input, U, this, 128, 128);
	}
	
	double compute_single_likelihood(const t_datum& d) override {
		auto v = call(d.input); // something of the type
		
		// average likelihood when sampling from this posterior
		return log( exp(v.lp(U)) * (1.0/10.0) + (1.0-d.reliability)/10.0 + d.reliability * exp(v.lp(d.output)) );
	}	

	
	virtual void print(std::string prefix="") override {
		
		// Hmm let's run over all the data and just get the modal response
		// for each data output from the training data set 
		std::map<word,DiscreteDistribution<word>> p; // probability of W given target
		extern std::vector<t_data> alldata;
		for(auto& di : alldata[alldata.size()-1]) {
			const word target = di.output; 
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

////////////////////////////////////////////////////////////////////////////////////////////
// Declare global set

Fleet::Statistics::TopN<MyHypothesis> all; // used by MCMC and MCTS locally

std::vector<MyHypothesis::t_data> alldata;

////////////////////////////////////////////////////////////////////////////////////////////
// If we want to do MCTS
class MyMCTS : public MCTSNode<MyMCTS, MyHypothesis> {
	using MCTSNode::MCTSNode;

	virtual void playout() override {

		MyHypothesis h0 = value; // need a copy to change resampling on 
		for(auto& n : h0.value ){
			n.can_resample = false;
		}
		
		// make a wrapper to add samples
		std::function<void(MyHypothesis& h)> cb = [&](MyHypothesis& h) { 
			if(h.posterior == -infinity) return;

			all << h;
			
			add_sample(h.posterior);
		};
		
		auto h = h0.copy_and_complete(); // fill in any structural gaps
		
		MCMCChain chain(h, data, cb);
		chain.run(Control(0, 1000, 1)); // run mcmc with restarts; we sure shouldn't run more than runtime
	}
	
};
////////////////////////////////////////////////////////////////////////////////////////////
// Make data 
////////////////////////////////////////////////////////////////////////////////////////////
MyHypothesis::t_datum sample_datum() { 
	
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

MyHypothesis::t_datum sample_adjusted_datum(std::function<double(MyHypothesis::t_datum&)>& f) {
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

////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) { 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Fancy number inference model");
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize();

	
	Grammar grammar(PRIMITIVES);
	
	typedef MyHypothesis::t_data  t_data;
	typedef MyHypothesis::t_datum t_datum;
	
	// Set up the data -- we'll do this so we can run a parallel
	// chain across all of it at once
	std::vector<Fleet::Statistics::TopN<MyHypothesis>>   alltops;
	for(auto ndata : data_amounts) {
		t_data mydata;
		
		// make some data here
		for(int di=0;di<ndata;di++) {
			// make the data point
			mydata.push_back(sample_datum());
		}

		alldata.push_back(mydata);
		alltops.push_back(Fleet::Statistics::TopN<MyHypothesis>(ntop));
	}
	t_data biggestData = *alldata.rbegin();
	

	// MCTS  - here just on the last data
//	MyHypothesis h0(&grammar);
//	MyMCTS m(explore, h0, &alldata[alldata.size()-1]);
//	tic();
//	m.parallel_search(Control(mcts_steps, runtime, nthreads));
//	tic();
//	m.print();
	
//	CERR "# Starting sampling." ENDL;
	
	
	// Run parallel tempering
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	ParallelTempering samp(h0, alldata, alltops);
	tic();
	samp.run(Control(mcmc_steps, runtime, nthreads), 200, 5000); 
	tic();

	// just simple mcmc on one
//	tic();
//	for(size_t di=0;di<alldata.size();di++){
//		MyHypothesis h0(&grammar);
//		h0 = h0.restart();
//		MCMCChain samp(h0, &alldata[0], alltops[0]);
//		samp.run(Control(mcmc_steps, runtime, nthreads)); 
//	}
//	tic();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
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
	
	COUT "# Computing posterior for extra data <= 4" ENDL;
	{
		std::function<double(t_datum&)> f = [](t_datum d) {
			return (d.output <= (word)4 ? 1.0 : 0.25);			
		};
		
		// make some data
		t_data newdata;
		for(size_t i=0;i<biggestData.size();i++) {
			auto di = sample_adjusted_datum(f);
			//cnt[di.output]++;
			newdata.push_back(di);
		}
		
		all.compute_posterior(newdata).print("lt4\t");
	}	
	
	COUT "# Computing posterior for extra data > 4" ENDL;
	{
		std::function<double(t_datum&)> f = [](t_datum d) {
			return (d.output > (word)4 ? 1.0 : 0.25);			
		};
		
		// make some data
		t_data newdata;
		for(size_t i=0;i<biggestData.size();i++) {
			auto di = sample_adjusted_datum(f);
			//cnt[di.output]++;
			newdata.push_back(di);
		}
		
		all.compute_posterior(newdata).print("gt4\t");
	}	
	
	
	

}

