enum class CustomOp {
	op_STREQ,op_EMPTYSTRING,op_EMPTY,op_A,op_CDR,op_CAR,op_CONS,op_NUM,
	op_REPEAT,op_INCR
};

// Define our types. 
#define NT_TYPES bool,std::string
#define NT_NAMES nt_bool,nt_string

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

using S = std::string; // just for convenience

S alphabet = "0123456789"; // the alphabet we use (possibly specified on command line)

// the data, comma separated; NOTE: does not handle empty strings yet
//S datastr  = "12345:23451,54335:43355"; 
//S datastr  = "12345:1122334455,567:556677"; 
//S datastr  = "12345:1223344556,513:561234"; 
//S datastr  = "12345:1121314151,7123:777172737";

//S datastr  = "12345:122333444455555,513:511333"; // probably this cannot be represented?

S datastr  = "12345:54321,7123:3217";


const double strgamma = 0.99; // penalty on string length

// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( Rule(nt_string, BuiltinOp::op_X,            "x",            {},                               10.0) );		
		add( Rule(nt_string, BuiltinOp::op_SAFE_RECURSE, "F(%s)",        {nt_string},                      2.0) );		

		for(size_t i=0;i<alphabet.length();i++)
			add( Rule(nt_string, CustomOp::op_A,    Q(alphabet.substr(i,1)),   {},                         10.0/alphabet.length(), i) );

		add( Rule(nt_string, CustomOp::op_EMPTYSTRING,  "\u00D8",           {},                               1.0) );
		
		add( Rule(nt_string, CustomOp::op_CONS,         "cons(%s,%s)",  {nt_string,nt_string},            1.0) );
		add( Rule(nt_string, CustomOp::op_CAR,          "car(%s)",      {nt_string},                      1.0) );
		add( Rule(nt_string, CustomOp::op_CDR,          "cdr(%s)",      {nt_string},                      1.0) );
		
		add( Rule(nt_string, BuiltinOp::op_IF,           "if(%s,%s,%s)", {nt_bool, nt_string, nt_string},  1.0) );
		
		add( Rule(nt_bool,   CustomOp::op_EMPTY,        "empty(%s)",    {nt_string},                      1.0) );
		add( Rule(nt_bool,   CustomOp::op_STREQ,        "(%s==%s)",     {nt_string,nt_string},            1.0) );
		
		add( Rule(nt_string,   CustomOp::op_REPEAT,        "rep(%s)",    {nt_string},                      1.0) );
		add( Rule(nt_string,   CustomOp::op_INCR,          "inc(%s)",    {nt_string},                      1.0) );
		
		// If we want to allow stochastic rules
//		add( Rule(nt_bool,   BuiltinOp::op_FLIP,         "flip()",       {},                               5.0) );
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
// * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_string, S, S> {
public:

	using Super = LOTHypothesis<MyHypothesis,Node,nt_string, S, S>;
	static const size_t MAX_LENGTH = 64; // longest strings cons will handle
	
	// I must implement all of these constructors
	MyHypothesis()                     : Super() {}
	MyHypothesis(Grammar* g)           : Super(g) {}
	MyHypothesis(Grammar* g, Node v)   : Super(g,v) {}
	

	double compute_single_likelihood(const t_datum& x) {
		auto out = call(x.input, "<err>", this, 256, 256); //256, 256);
		
		// a likelihood based on the prefix probability -- we assume that we generate from the hypothesis
		// and then might glue on some number of additional strings, flipping a gamma-weighted coin to determine
		// how many to add
		double lp = -infinity;
		for(auto o : out.values()) { // add up the probability from all of the strings
			if(is_prefix(o.first, x.output)) {
				lp = logplusexp(lp, o.second + log(strgamma) + log((1.0-strgamma)/alphabet.length()) * (x.output.length() - o.first.length()));
			}
		}
		return lp;
	}
	
	abort_t dispatch_rule(Instruction i, VirtualMachinePool<S,S>* pool, VirtualMachineState<S,S>& vms, Dispatchable<S,S>* loader ) {
		switch(i.getCustom()) {
			CASE_FUNC0(CustomOp::op_A,           S,          [i](){ return alphabet.substr(i.arg, 1);} )
			CASE_FUNC0(CustomOp::op_EMPTYSTRING, S,          [](){ return S("");} )
			CASE_FUNC1(CustomOp::op_EMPTY,       bool,  S,   [](const S& s){ return s.size()==0;} )
			CASE_FUNC2(CustomOp::op_STREQ,       bool,  S,S, [](const S& a, const S& b){return a==b;} )
			CASE_FUNC1(CustomOp::op_CDR,         S, S,       [](const S& s){ return (s.empty() ? S("") : s.substr(1,S::npos)); } )		
			CASE_FUNC1(CustomOp::op_CAR,         S, S,       [](const S& s){ return (s.empty() ? S("") : S(1,s.at(0))); } )		
			CASE_FUNC1(CustomOp::op_INCR,        S, S,       [](const S& s){ 
				S out = s;
				for(size_t i=0;i<s.size();i++) {
					auto p = alphabet.find(s.at(i));
					p++; // next position
					if(p == std::string::npos) p = 0;
					out[i] = alphabet[p];
				}
				return out;
			} )		
			
			CASE_FUNC2e(CustomOp::op_CONS,       S, S,S,
								[](const S& x, const S& y){ S a = x; a.append(y); return a; },
								[](const S& x, const S& y){ return (x.length()+y.length()<MAX_LENGTH ? abort_t::NO_ABORT : abort_t::SIZE_EXCEPTION ); }
								)
			CASE_FUNC1e(CustomOp::op_REPEAT,       S, S,
								[](const S& x){ return x+x; },
								[](const S& x){ return (2*x.length()<MAX_LENGTH ? abort_t::NO_ABORT : abort_t::SIZE_EXCEPTION ); }
								)
								
			default: assert(0 && " *** You ended up with an invalid argument"); // should never get here
		}
		return abort_t::NO_ABORT;
	}
	
	void print(std::string prefix="") {
		auto out = call("12345", "<err>", this, 256, 256);
		Super::print(out.string() + "\t");
	}
};


// mydata stores the data for the inference model
MyHypothesis::t_data mydata;
// top stores the top hypotheses we have found
TopN<MyHypothesis> top;


// This gets called on every sample -- here we add it to our best seen so far (top) and
// print it every thin samples unless thin=0
void callback(MyHypothesis& h) {
	
	// add to the top
	top << h; 
	
	// print out with thinning
	if(thin > 0 && FleetStatistics::global_sample_count % thin == 0) 
		h.print();
}

////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	using namespace std;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments();
	app.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	app.add_option("-d,--data", datastr, "Comma separated list of input data strings");	
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); // set by above macro

	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	// declare a grammar
	MyGrammar grammar;
	
	//------------------
	// set up the data
	//------------------
	
	// we will parse the data from a comma-separated list of "data" on the command line
	for(auto di : split(datastr, ',')) {
		auto io = split(di, ':'); // written in input:output format
		assert(io.size()==2);
		mydata.push_back( MyHypothesis::t_datum({io[0], io[1]}) );
	}
	
	//------------------
	// Run
	//------------------
	

//	auto h0 = new MyHypothesis(&grammar);	
//	auto thechain = MCMCChain(h0, &mydata, callback);
//	thechain.run(mcmc_steps, runtime);
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	ParallelTempering<MyHypothesis> samp(h0, &mydata, callback, 8, 1000.0, false);
	tic();
	samp.run(mcmc_steps, runtime, 0.2, 3.0); //30000);		
	tic();

//	auto h0 = new MyHypothesis(&grammar, nullptr); // start with empty node
// 	MCTSNode<MyHypothesis> m(explore, h0, playout);
//	tic();
//	m.parallel_search(nthreads, mcts_steps, runtime);
//	tic();
//	m.print("tree.txt");
	
	// Show the best we've found
	top.print();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
}