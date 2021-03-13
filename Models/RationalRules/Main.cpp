

///########################################################################################
// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second on 4 threads
///########################################################################################

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the MyObject features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// This is a default object with three dimensions (shape, color, size) and three values for each
#include "ShapeColorSizeObject.h"

using MyObject = ShapeColorSizeObject;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,MyObject)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<MyObject,bool,   MyObject, bool>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("blue(%s)",       +[](MyObject x) -> bool { return x.is(Color::Blue); });
		add("yellow(%s)",     +[](MyObject x) -> bool { return x.is(Color::Yellow); });
		add("green(%s)",      +[](MyObject x) -> bool { return x.is(Color::Green); });
		
		add("rectangle(%s)",  +[](MyObject x) -> bool { return x.is(Shape::Rectangle); });
		add("triangle(%s)",   +[](MyObject x) -> bool { return x.is(Shape::Triangle); });
		add("circle(%s)",     +[](MyObject x) -> bool { return x.is(Shape::Circle); });
		
		add("size1(%s)",      +[](MyObject x) -> bool { return x.is(Size::size1); });
		add("size2(%s)",      +[](MyObject x) -> bool { return x.is(Size::size2); });
		add("size3(%s)",      +[](MyObject x) -> bool { return x.is(Size::size3); });
		
		// These are the short-circuiting versions:
		// these need to know the grammar type
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);

		add("x",             Builtins::X<MyGrammar>);
	}
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
	// set up the data
	//------------------
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;

	mydata.emplace_back(MyObject("triangle-blue-1"),   true, 0.75);
	mydata.emplace_back(MyObject("circle-blue-1"),     false, 0.75);
	mydata.emplace_back(MyObject("circle-yellow-1"),   false, 0.75);
	
	//------------------
	// Run
	//------------------
	
	MyGrammar grammar;

	TopN<MyHypothesis> top;
	
	
//	auto h0 = MyHypothesis::make(&grammar);
//	ChainPool chain(h0, &mydata, top, FleetArgs::nchains);
//	tic();
//	chain.run(Control());
//	tic();
//	
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