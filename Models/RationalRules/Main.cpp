#include <assert.h>

// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second

// This is also used to check MCMC, as it prints out counts and log posteriors which
// we can check for match

// We require an enum to define our custom operations as a string before we import Fleet
// These are the internal symbols that are used to represent each operation. The grammar
// generates nodes with them, and then dispatch down below gets called with a switch
// statement to see how to execute aech of them. 
enum class CustomOp {
	op_And, op_Or, op_Not, 
	op_Red, op_Green, op_Blue, 
	op_Square, op_Triangle, op_Circle
};

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};

typedef struct Object {
	Color color;
	Shape shape;
	
	// we must define this to compile because memoization requires sorting
	// but if we don't call op_MEM then we never will need it
	bool operator<(const Object& o) const { assert(false); }
} Object;

// Define our types. 
#define NT_TYPES bool, Object, int
#define NT_NAMES nt_bool,nt_object, nt_value

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 
//#include "GrammarInference.h"

// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( Rule(nt_object, BuiltinOp::op_X,         "x",             {},                   1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Red,        "red(%s)",       {nt_object},          1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Green,      "green(%s)",     {nt_object},          1.0) );
		add( Rule(nt_bool,   CustomOp::op_Blue,       "blue(%s)",      {nt_object},          1.0) );
		
		add( Rule(nt_bool,   CustomOp::op_Square,     "square(%s)",    {nt_object},          1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Triangle,   "triangle(%s)",  {nt_object},          1.0) );
		add( Rule(nt_bool,   CustomOp::op_Circle,     "circle(%s)",    {nt_object},          1.0) );
		
		add( Rule(nt_bool, CustomOp::op_And,          "(%s and %s)",   {nt_bool, nt_bool},   1.0) );
		add( Rule(nt_bool, CustomOp::op_Or,           "(%s or %s)",    {nt_bool, nt_bool},   1.0) );
		add( Rule(nt_bool, CustomOp::op_Not,          "not(%s)",       {nt_bool},            1.0) );
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool> {
public:
	using Super = LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool>;
	MyHypothesis(Grammar* g, Node n)   : Super(g, n) {}
	MyHypothesis(Grammar* g)           : Super(g) {}
	MyHypothesis()                     : Super() {}
	
	double compute_single_likelihood(const t_datum& x) {
		bool out = callOne(x.input, false);
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
	}
	
	abort_t dispatch_rule(Instruction i, VirtualMachinePool<Object, bool>* pool, VirtualMachineState<Object,bool>& vms, Dispatchable<Object,bool>* loader ) {
		switch(i.getCustom()) {
			CASE_FUNC1(CustomOp::op_Red,         bool,  Object,    [](const Object& x){ return x.color == Color::Red; })
			CASE_FUNC1(CustomOp::op_Green,       bool,  Object,    [](const Object& x){ return x.color == Color::Green; })
			CASE_FUNC1(CustomOp::op_Blue,        bool,  Object,    [](const Object& x){ return x.color == Color::Blue; })
			
			CASE_FUNC1(CustomOp::op_Square,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Square; })
			CASE_FUNC1(CustomOp::op_Triangle,    bool,  Object,    [](const Object& x){ return x.shape == Shape::Triangle; })
			CASE_FUNC1(CustomOp::op_Circle,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Circle; })
			
			CASE_FUNC2(CustomOp::op_And,         bool,bool,bool,    [](const bool a, bool b){ return a&&b; })
			CASE_FUNC2(CustomOp::op_Or,          bool,bool,bool,    [](const bool a, bool b){ return a||b; })
			CASE_FUNC1(CustomOp::op_Not,         bool,bool,         [](const bool a){ return !a; })
			
			default:
				assert(0 && " *** You used an invalid operation"); // should never get here
		}
		return abort_t::NO_ABORT;
	}
	
	void print(std::string prefix="") {
		extern TopN<MyHypothesis> top;
		Super::print( prefix + std::to_string(hash())+"\t"+std::to_string(top.count(*this)) + "\t" ); // print but prepend my top count
	}
};


// mydata stores the data for the inference model
MyHypothesis::t_data mydata;
// top stores the top hypotheses we have found
TopN<MyHypothesis> top;

// This gets called on every sample -- here we add it to our best seen so far (top) and
// print it every thin samples unless thin=0
void callback(MyHypothesis& h) {
	top << h; // add to the top
}


////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments();
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); // set by above macro

	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	//------------------
	// declare a grammar
	//------------------
	MyGrammar grammar;
	
	//------------------
	// set up the data
	//------------------
	
	mydata.push_back(   (MyHypothesis::t_datum){ (Object){Color::Red, Shape::Triangle}, true,  0.75 }  );
	mydata.push_back(   (MyHypothesis::t_datum){ (Object){Color::Red, Shape::Square},   false, 0.75 }  );
	mydata.push_back(   (MyHypothesis::t_datum){ (Object){Color::Red, Shape::Square},   false, 0.75 }  );
	
	// just sample from the prior
//	MyHypothesis h0(&grammar);
//	for(size_t i=0;i<mcmc_steps;i++) {
//		h0 = h0.restart(); // sample from prior
//		h0.compute_posterior(mydata);
//		callback(h0);
//	}
//	
	
//	MyHypothesis h0(&grammar);
//	h0 = h0.restart();
//	MCMCChain<MyHypothesis> chain(h0, &mydata, callback);
//	tic();
//	chain.run(mcmc_steps,runtime);
//	tic();
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	ParallelTempering<MyHypothesis> samp(h0, &mydata, callback, 8, 1000.0, false);
	tic();
	samp.run(mcmc_steps, runtime, 0.2, 3.0); //30000);		
	tic();
//	
	// Show the best we've found
	top.print();
		
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
}