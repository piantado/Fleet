
// We require an enum to define our custom operations as a string before we import Fleet
// These are the internal symbols that are used to represent each operation. The grammar
// generates nodes with them, and then dispatch down below gets called with a switch
// statement to see how to execute aech of them. 
enum class CustomOp {
	op_STREQ,op_EMPTYSTRING,op_EMPTY,op_A,op_CDR,op_CAR,op_CONS,op_REPEAT,op_NUM
};

// Define our types. 
#define NT_TYPES bool,std::string
#define NT_NAMES nt_bool,nt_string

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

using S = std::string; // just for convenience

S alphabet = "01"; // the alphabet we use (possibly specified on command line)
//S datastr  = "01,01011,010110111"; // the data, comma separated
S datastr  = "011,011011,011011011"; // the data, comma separated
const double strgamma = 0.99; // penalty on string length

// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( Rule(nt_string, BuiltinOp::op_X,            "x",            {},                               10.0) );		

		// here we create an alphabet op with an "arg" that stores the character (this is fater than alphabet.substring with i.arg as an index) 
		for(size_t i=0;i<alphabet.length();i++)
			add( Rule(nt_string, CustomOp::op_A,            alphabet.substr(i,1),          {},                   10.0/alphabet.length(), (int)alphabet.at(i) ) );

		add( Rule(nt_string, CustomOp::op_EMPTYSTRING,  "''",           {},                               1.0) );
		
		add( Rule(nt_string, CustomOp::op_CONS,         "cons(%s,%s)",  {nt_string,nt_string},            1.0) );
		add( Rule(nt_string, CustomOp::op_CAR,          "car(%s)",      {nt_string},                      1.0) );
		add( Rule(nt_string, CustomOp::op_CDR,          "cdr(%s)",      {nt_string},                      1.0) );
		
		add( Rule(nt_string, BuiltinOp::op_IF,          "if(%s,%s,%s)", {nt_bool, nt_string, nt_string},  1.0) );
		
		add( Rule(nt_bool,   BuiltinOp::op_FLIP,        "flip()",       {},                               5.0) );
		add( Rule(nt_bool,   CustomOp::op_EMPTY,        "empty(%s)",    {nt_string},                      1.0) );
		add( Rule(nt_bool,   CustomOp::op_STREQ,        "(%s==%s)",     {nt_string,nt_string},            1.0) );
		
		add( Rule(nt_string, BuiltinOp::op_RECURSE,      "F(%s)",        {nt_string},                      2.0) );		

	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
// * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_string,S,S> {
public:
	using Super =  LOTHypothesis<MyHypothesis,Node,nt_string,S,S>;
	
	static const size_t MAX_LENGTH = 64; // longest strings cons will handle
	
	// I must implement all of these constructors
	MyHypothesis(Grammar* g, Node v)    : Super(g,v) {}
	MyHypothesis(Grammar* g)            : Super(g) {}
	MyHypothesis()                      : Super() {}
	
	// Very simple likelihood that just counts up the probability assigned to the output strings
//	double compute_single_likelihood(const t_datum& x) {
//		auto out = call(x.input, "<err>");
//		return logplusexp( log(x.reliability)+(out.count(x.output)?out[x.output]:-infinity),  // probability of generating the output
//					       log(1.0-x.reliability) + (x.output.length())*(log(1.0-gamma) + log(0.5)) + log(gamma) // probability under noise; 0.5 comes from alphabet size
//						   );
//	}
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
		/* Dispatch the functions that I have defined. Returns NO_ABORT on success. 
		 * */
		switch(i.getCustom()) {
			// this uses CASE_FUNCs which are defined in CaseMacros.h, and which give a nice interface for processing the
			// different cases. They take a return type, some input types, and a functino to evaluate. 
			// as in op_CONS below, this function (CASE_FUNC2e) can take an "error" (e) condition, which will cause
			// an abort			
			
			// when we process op_A, we unpack the "arg" into an index into alphabet
			CASE_FUNC0(CustomOp::op_A,           S,          [i](){ return std::string(1,(char)i.arg);}) // alphabet.substr(i.arg, 1);} )
			// the rest are straightforward:
			CASE_FUNC0(CustomOp::op_EMPTYSTRING, S,          [](){ return S("");} )
			CASE_FUNC1(CustomOp::op_EMPTY,       bool,  S,   [](const S& s){ return s.size()==0;} )
			CASE_FUNC2(CustomOp::op_STREQ,       bool,  S,S, [](const S& a, const S& b){return a==b;} )
			CASE_FUNC1(CustomOp::op_CDR,         S, S,       [](const S& s){ return (s.empty() ? S("") : s.substr(1,S::npos)); } )		
			CASE_FUNC1(CustomOp::op_CAR,         S, S,       [](const S& s){ return (s.empty() ? S("") : S(1,s.at(0))); } )		
//			CASE_FUNC2e(CustomOp::op_REPEAT,      S,  S, int, [](const S& a, const int i){
//				S out;
//				for(int j=0;j<i;j++) out = out + a;
//				return out;				
//				},
//				[](const S& x, const int i){ return (x.length()*i<MAX_LENGTH ? abort_t::NO_ABORT : abort_t::SIZE_EXCEPTION ); }
//			)
//			CASE_FUNC0(CustomOp::op_NUM,         int,       [i](){ return i.arg; } )		
			
			
//			CASE_FUNC2e(CustomOp::op_CONS,       S, S,S,
//								[](const S& x, const S& y){ S a = x; a.append(y); return a; },
//								[](const S& x, const S& y){ return (x.length()+y.length()<MAX_LENGTH ? abort_t::NO_ABORT : abort_t::SIZE_EXCEPTION ); }
//								)
			case CustomOp::op_CONS: {
				// Above is one way to implement cons, but here we can do it faster by not removing it from the stack
				S y = vms.getpop<S>();
				
				// length check
				if(vms.stack<S>().topref().length() + y.length() > MAX_LENGTH) 
					return abort_t::SIZE_EXCEPTION;

				vms.stack<S>().topref().append(y);
				
				break;
			}
			default:
				assert(0 && " *** You ended up with an invalid argument"); // should never get here
		}
		return abort_t::NO_ABORT;
	}
	
	void print(std::string prefix="") {
		extern TopN<MyHypothesis> top;
		assert(prefix == "");
		
		prefix  = "#\n#" +  this->call("", "<err>").string() + "\n"; 
		prefix += std::to_string(top.count(*this)) + "\t";
		
		Super::print(prefix); // print but prepend my top count
	}
};


// mydata stores the data for the inference model
MyHypothesis::t_data mydata;
// top stores the top hypotheses we have found
TopN<MyHypothesis> top;


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
		mydata.push_back( MyHypothesis::t_datum({S(""), di}) );
	}
	
	//------------------
	// Run
	//------------------
	
	MyHypothesis h0(&grammar);
//
//	tic();
//	MCMCChain thechain(h0, &mydata, top);
//	thechain.run(mcmc_steps, runtime);
//	tic();
//	
	ParallelTempering samp(h0, &mydata, top, nthreads, 1000.0);
	tic();
	samp.run(mcmc_steps, runtime, 1.0, 3.0); //30000);		
	tic();
	

//	std::function<void(const MyHypothesis& h)> cb = [](const MyHypothesis& h) {return;};
//	tic();
//	ChainPool samp(h0, &mydata, cb, nthreads);
//	samp.run(mcmc_steps, runtime);
//	tic();
	
	// Show the best we've found
	top.print();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
}