
// We require a macro to define our custom operations as a string BEFORE we import Fleet
enum CustomOp {
	op_STREQ,op_EMPTYSTRING,op_EMPTY,op_A,op_B,op_CDR,op_CAR,op_CONS
};

// Define our types. 
#define NT_TYPES bool,std::string
#define NT_NAMES nt_bool,nt_string

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

using S = std::string; // just for convenience

// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( new Rule(nt_string, op_X,            "x",            {},                               5.0) );		
		add( new Rule(nt_string, op_RECURSE,      "F(%s)",        {nt_string},                      2.0) );		

		add( new Rule(nt_string, op_A,            "'0'",          {},                               2.50) );
		add( new Rule(nt_string, op_B,            "'1'",          {},                               2.50) );
		add( new Rule(nt_string, op_EMPTYSTRING,  "''",           {},                               1.0) );
		
		add( new Rule(nt_string, op_CONS,         "cons(%s,%s)",  {nt_string,nt_string},            1.0) );
		add( new Rule(nt_string, op_CAR,          "car(%s)",      {nt_string},                      1.0) );
		add( new Rule(nt_string, op_CDR,          "cdr(%s)",      {nt_string},                      1.0) );
		
		add( new Rule(nt_string, op_IF,           "if(%s,%s,%s)", {nt_bool, nt_string, nt_string},  1.0) );
		
		add( new Rule(nt_bool,   op_FLIP,         "flip()",       {},                               5.0) );
		add( new Rule(nt_bool,   op_EMPTY,        "empty(%s)",    {nt_string},                      1.0) );
		add( new Rule(nt_bool,   op_STREQ,        "(%s==%s)",     {nt_string,nt_string},            1.0) );
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_string, S, S> {
public:

	static const size_t MAX_LENGTH = 64; // longest strings cons will handle
	static constexpr double gamma      = 0.99; // this coin flip says when we end 
	static const S err;
	
	// I must implement all of these constructors
	MyHypothesis(Grammar* g)            : LOTHypothesis<MyHypothesis,Node,nt_string,S,S>(g) {}
	MyHypothesis(Grammar* g, Node* v)   : LOTHypothesis<MyHypothesis,Node,nt_string,S,S>(g,v) {}
	
	double compute_single_likelihood(const t_datum& x) {
		// Very simple likelihood that just counts up the probability assigned to the output strings
		auto out = call(x.input, err);
		return logplusexp( log(x.reliability)+(out.count(x.output)?out[x.output]:-infinity),  // probability of generating the output
					       log(1.0-x.reliability) + (x.output.length())*(log(1.0-gamma) + log(0.5)) + log(gamma) // probability under noise; 0.5 comes from alphabet size
						   );
	}
	
	abort_t dispatch_rule(Instruction i, VirtualMachinePool<S,S>* pool, VirtualMachineState<S,S>* vms, Dispatchable<S,S>* loader ) {
		/* Dispatch the functions that I have defined. Returns NO_ABORT on success. 
		 * */
		assert(i.is_custom);
		switch(i.custom) {
			CASE_FUNC0(op_A,           S,          [](){ return S("0");} )
			CASE_FUNC0(op_B,           S,          [](){ return S("1");} )
			CASE_FUNC0(op_EMPTYSTRING, S,          [](){ return S("");} )
			CASE_FUNC1(op_EMPTY,       bool,  S,   [](const S s){ return s.size()==0;} )
			CASE_FUNC2(op_STREQ,       bool,  S,S, [](const S a, const S b){return a==b;} )
			CASE_FUNC1(op_CDR,         S, S,       [](const S s){ return (s.empty() ? S("") : s.substr(1,S::npos)); } )		
			CASE_FUNC1(op_CAR,         S, S,       [](const S s){ return (s.empty() ? S("") : S(1,s.at(0))); } )		
			CASE_FUNC2e(op_CONS,       S, S,S,
								[](const S x, const S y){ S a = x; a.append(y); return a; },
								[](const S x, const S y){ return (x.length()+y.length()<MAX_LENGTH ? NO_ABORT : SIZE_EXCEPTION ); }
								)
			default:
				assert(0); // should never get here
		}
		return NO_ABORT;
	}
};
const S MyHypothesis::err = "<err>";


MyHypothesis::t_data mydata;
TopN<MyHypothesis> top;


void print(MyHypothesis& h) {
	COUT "# ";
	h.call("", "<err>").print();
	COUT "\n" << top.count(h) TAB  h.posterior TAB h.prior TAB h.likelihood TAB h.string() ENDL;
}


// This gets called on every sample -- here we add it to our best seen so far (top) and
// print it every thin samples unless thin=0
void callback(MyHypothesis* h) {
	
	top << *h; 
	
	// print out with thinning
	if(thin > 0 && FleetStatistics::global_sample_count % thin == 0) 
		print(*h);
}


////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	using namespace std;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	FLEET_DECLARE_GLOBAL_ARGS()
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); // set by above macro

	Fleet_initialize();
	
	MyGrammar grammar;
	
	//------------------
	// set up the data
	//------------------
	
	// simple pattern
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("01011"), 0.99}) );
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("0101101011"), 0.99}) );
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("010110101101011"), 0.99}) );

	// Fibonacci data:
	mydata.push_back( MyHypothesis::t_datum({S(""), S("01011"), 0.99}) );
	mydata.push_back( MyHypothesis::t_datum({S(""), S("010110111"), 0.99}) );
	mydata.push_back( MyHypothesis::t_datum({S(""), S("010110111011111"), 0.99}) );
	mydata.push_back( MyHypothesis::t_datum({S(""), S("010110111011111011111111"), 0.99}) );

	// increasing count data
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("01011"), 0.99}) );
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("010110111"), 0.99}) );
//	mydata.push_back( MyHypothesis::t_datum({S(""), S("01011011101111"), 0.99}) );

	//------------------
	// Run
	//------------------
	
	auto h0 = new MyHypothesis(&grammar);
	
	tic(); // start the timer
	parallel_MCMC(nthreads, h0, &mydata, callback, mcmc_steps, mcmc_restart);
//	MCMC<MyHypothesis>(h0, mydata, callback, mcmc_steps);
	tic(); // end timer
	
	// Show the best we've found
	top.print(print);
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
}