
#include <string>

using S = std::string; // just for convenience

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables (which may get overwritten)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

S alphabet = "0123456789"; 
S datastr  = "011,011011,011011011"; // the data, comma separated
const double strgamma = 0.99; // penalty on string length
const size_t MAX_LENGTH = 64; // longest strings cons will handle
	
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define FLEET_GRAMMAR_TYPES S,bool

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"

std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : s.substr(1,S::npos)); }),
	Primitive("head(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : S(1,s.at(0))); }),
	Primitive("pair(%s,%s)",   +[](S a, S b) -> S { if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError;
													return a+b; }),
	Primitive("\u00D8",        +[]()         -> S { return  S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool { return x==y; }),
};

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,S,S> {
public:
	using Super =  LOTHypothesis<MyHypothesis,Node,S,S>;
	using Super::Super;
	
	double compute_single_likelihood(const t_datum& x) {
		auto out = callOne(x.input, "<err>"); // NOTE NOT STOCHASTIC

		if(is_prefix(out, x.output)) {
			return log((1.0-strgamma)/alphabet.length()) * (x.output.length() - out.length());
		}
		else {
			return -infinity;
		}
	}
	void print(std::string prefix="") {
		// we're going to make this print by showing the language we created on the line before
		prefix  = prefix+"#\n#" +  this->call("", "<err>").string() + "\n"; 
		Super::print(prefix); 
	}
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

S decomma(S& s) {
	// conver 1,2,3 to 123 and force everything into the ascii range 48-125
	auto parts = split(s, ',');
	S out = "";
	for(const auto& x : parts) {
		char c = (char)std::stoi(x);
		
		// if we want to remap to a reasonable space...
		c = 48 + (c%(128-48));
		
		out += S(1,c);
	}
	return out;
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 

	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("A simple, one-factor formal language learner");
	app.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	app.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	// mydata stores the data for the inference model
	MyHypothesis::t_data mydata;
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top(ntop);
	
	// declare a grammar with our primitives
	Grammar grammar(PRIMITIVES);
	
	// Add the builtins we want
	grammar.add<S>         (BuiltinOp::op_X,            "x", 10.0); 
	grammar.add<S,bool,S,S>(BuiltinOp::op_IF,           "if(%s,%s,%s)"); 
	grammar.add<S,S>       (BuiltinOp::op_SAFE_RECURSE, "F(%s)"); 
	
	// here we create an alphabet op with an "arg" that stores the character (this is fater than alphabet.substring with i.arg as an index) 
	// here, op_ALPHABET converts arg to a string (and pushes it)
	for(size_t i=0;i<alphabet.length();i++) {
		grammar.add<S>     (BuiltinOp::op_ALPHABET, alphabet.substr(i,1), 10.0/alphabet.length(), (int)alphabet.at(i)); 
	}
	
	//------------------
	// set up the data
	//------------------
	
	{
		FILE* fp = fopen(input_path.c_str(), "r");
		if(fp==NULL) { fprintf(stderr, "*** ERROR: Cannot open file!"); exit(1);}
		char* line = NULL; size_t len=0; 
		while( getline(&line, &len, fp) != -1 ) {
			if( line[0] == '#' ) continue;  // skip comments
			line[strcspn(line, "\n")] = 0; // remove newline
			
			// convert 1,2,3 format to 123 (separate chars)
			// NOTE: FOR NOW we take everything in the ascii range 48-90
			
			auto io = split(S(line),';');
			assert(io.size() == 2 && "*** Did not get exactly two parts (input/output) in data");
			auto i = decomma(io[0]);
			auto o = decomma(io[1]);
			
			COUT "# using data " << i <<" -> "<< o ENDL;
			
			mydata.push_back( MyHypothesis::t_datum({i, o}) );
		}
		fclose(fp);
	}
	
	//------------------
	// Run
	//------------------
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	
	ParallelTempering samp(h0, &mydata, top, nchains, 1000.0);
	tic();
	samp.run(mcmc_steps, runtime, 1.0, 3.0); //30000);		
	tic();
	
	// Show the best we've found
	top.print();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;
}



































