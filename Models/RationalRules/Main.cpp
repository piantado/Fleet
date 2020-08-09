

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
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
/// This must be defined before we import Fleet because Fleet does some template
/// magic internally
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }, 2.0), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	// that + is really insane, but is needed to convert a lambda to a function pointer

	Primitive("red(%s)",       +[](MyObject x)       -> bool { return x.is(Color::Red); }),
	Primitive("green(%s)",     +[](MyObject x)       -> bool { return x.is(Color::Green); }),
	Primitive("blue(%s)",      +[](MyObject x)       -> bool { return x.is(Color::Blue); }),

	Primitive("square(%s)",    +[](MyObject x)       -> bool { return x.is(Shape::Square); }),
	Primitive("triangle(%s)",  +[](MyObject x)       -> bool { return x.is(Shape::Triangle); }),
	Primitive("circle(%s)",    +[](MyObject x)       -> bool { return x.is(Shape::Circle); }),
	
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<MyObject>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,MyObject)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,MyObject> {
	using Super =  Grammar<bool,MyObject>;
	using Super::Super;
};


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
	// value with probability x.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& x) override {
		bool out = callOne(x.input, false);
		return log((1.0-x.reliability)/2.0) + (out == x.output)*x.reliability);
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Rational rules");
	fleet.initialize(argc, argv);
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)
	// in doing this, grammar deduces the types from the input and output types of each primitive
	MyGrammar grammar(PRIMITIVES);
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top(ntop);
	
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
	
//	MyHypothesis h0(&grammar);
//	h0 = h0.restart();
//	MCMCChain chain(h0, &mydata, top);
//	tic();
//	chain.run(Control(mcmc_steps,runtime));
//	tic();
//	
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	ParallelTempering samp(h0, &mydata, top, 16, 10.0); 
	tic();
	samp.run(Control(mcmc_steps,runtime,nthreads), 100, 1000); 		
	tic();

	// Show the best we've found
	top.print();
}