

///########################################################################################
// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second on 4 threads
///########################################################################################


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the MyObject features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};

#include "Object.h"

typedef Object<Color,Shape> MyObject;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,MyObject)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

using MyGrammar = Grammar<MyObject,bool,   MyObject, bool>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyObject,bool,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,MyObject,bool,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability di.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& di) override {
		bool out = callOne(di.input, false);
		return log((1.0-di.reliability)/2.0 + (out == di.output)*di.reliability);
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 
#include "Builtins.h"

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Rational rules");
	fleet.initialize(argc, argv);
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)
	// in doing this, grammar deduces the types from the input and output types of each primitive
	MyGrammar grammar;
	grammar.add("red(%s)",       +[](MyObject x) -> bool { return x.is(Color::Red); });
	grammar.add("green(%s)",     +[](MyObject x) -> bool { return x.is(Color::Green); });
	grammar.add("blue(%s)",      +[](MyObject x) -> bool { return x.is(Color::Blue); });
	grammar.add("square(%s)",    +[](MyObject x) -> bool { return x.is(Shape::Square); });
	grammar.add("triangle(%s)",  +[](MyObject x) -> bool { return x.is(Shape::Triangle); });
	grammar.add("circle(%s)",    +[](MyObject x) -> bool { return x.is(Shape::Circle); });
	
	// These are the short-circuiting versions:
	// these need to know the grammar type
	grammar.add("and(%s,%s)",    Builtins::And<MyGrammar>);
	grammar.add("or(%s,%s)",     Builtins::Or<MyGrammar>);
	grammar.add("not(%s)",       Builtins::Not<MyGrammar>);

	grammar.add("x",             Builtins::X<MyGrammar>);
	

	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top;
	
	//------------------
	// set up the data
	//------------------
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Triangle}, .output=true,  .reliability=0.75});
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Square},   .output=false, .reliability=0.75});
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Square},   .output=false, .reliability=0.75});
	
	//------------------
	// Actually run
	//------------------
	
//	auto h0 = MyHypothesis::make(&grammar);
//	MCMCChain chain(h0, &mydata, top);
//	tic();
//	chain.run(Control());
//	tic();
//	
	auto h0 = MyHypothesis::make(&grammar);
	ParallelTempering samp(h0, &mydata, top, 16, 10.0); 
	tic();
	samp.run(Control(), 100, 1000); 		
	tic();

	// Show the best we've found
	top.print();
}