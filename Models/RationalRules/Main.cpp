// - update description of primitives, instructions, etc. 



// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second

#include "assert.h"

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};

typedef struct Object {
	Color color;
	Shape shape;
	
	// we must define this to compile because memoization requires sorting
	// but if we don't call op_MEM then we never will need it
	bool operator<(const Object& o) const { assert(false); }
} Object;

// These define all of the types that are used in the grammar -- must be defined
// before we import Fleet
#define FLEET_GRAMMAR_TYPES bool,Object

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return a && b; }, 2.0),
	Primitive("blah(%s,%s)",   +[](bool a, bool b) -> bool { return a && b; }),
	Primitive("red(%s)",       +[](Object x)       -> bool { return x.color == Color::Red; }),
};
// that + is really insane, but is needed to convert a lambda to a function pointer
//
///// NOW Pass this to Grammar and build it in to 
Grammar grammar(PRIMITIVES);


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,Object,bool> {
public:
	using Super = LOTHypothesis<MyHypothesis,Node,Object,bool>;
	MyHypothesis(Grammar* g, Node n)   : Super(g, n) {}
	MyHypothesis(Grammar* g)           : Super(g) {}
	MyHypothesis()                     : Super() {}
	
	double compute_single_likelihood(const t_datum& x) {
		bool out = callOne(x.input, false);
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
	}
	
	
	abort_t dispatch_rule(Instruction i, VirtualMachinePool<Object, bool>* pool, VirtualMachineState<Object,bool>& vms, Dispatchable<Object,bool>* loader ) {
	
		auto op = i.getCustom();
		
		switch(op) {
//			if constexpr (std::tuple_size<PRIMITIVES>::value > 0) { case 0: return std::get<0>(PRIMITIVES).VMScall(vms); };
//			if constexpr(std::tuple_size<PRIMITIVES>::value > 1) { case 1: return std::get<1>(PRIMITIVES).VMScall(vms); };
//			if constexpr(std::tuple_size<PRIMITIVES>::value > 2) { case 2: return std::get<2>(PRIMITIVES).VMScall(vms); };
//			if constexpr(std::tuple_size<PRIMITIVES>::value > 3) { case 3: return std::get<3>(PRIMITIVES).VMScall(vms); };
//			if constexpr(std::tuple_size<PRIMITIVES>::value > 4) { case 4: return std::get<4>(PRIMITIVES).VMScall(vms); };
		}
		
//		switch(i.getCustom()) {

//			case 0: return std::get<0>(PRIMITIVES).VMScall(vms);
//			case 1: return std::get<1>(PRIMITIVES).VMScall(vms);
//			case 2: return std::get<2>(PRIMITIVES).VMScall(vms);
//			case 3: return std::get<3>(PRIMITIVES).VMScall(vms);
			
//			CASE(my_and)
			
			//case my_and.op:	return my_and.VMScall(vms); 
			
			// We want a syntax like:
			// case grammar.getOp("and"): return my_and(x,y)
			// CASE_FUNC("and", my_and)
			
			// Maybe syntax like CASEFUNC1
			// CASE(my_and) // extract everything from the Primitive class
			
//			CASE_FUNC1(CustomOp::op_Red,         bool,  Object,    [](const Object& x){ return x.color == Color::Red; })
//			CASE_FUNC1(CustomOp::op_Green,       bool,  Object,    [](const Object& x){ return x.color == Color::Green; })
//			CASE_FUNC1(CustomOp::op_Blue,        bool,  Object,    [](const Object& x){ return x.color == Color::Blue; })
//			
//			CASE_FUNC1(CustomOp::op_Square,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Square; })
//			CASE_FUNC1(CustomOp::op_Triangle,    bool,  Object,    [](const Object& x){ return x.shape == Shape::Triangle; })
//			CASE_FUNC1(CustomOp::op_Circle,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Circle; })
//			
//			CASE_FUNC2(CustomOp::op_And,         bool,bool,bool,    [](const bool a, bool b){ return a&&b; })
//			CASE_FUNC2(CustomOp::op_Or,          bool,bool,bool,    [](const bool a, bool b){ return a||b; })
//			CASE_FUNC1(CustomOp::op_Not,         bool,bool,         [](const bool a){ return !a; })
			
//			default:
//				assert(0 && " *** You used an invalid operation"); // should never get here
//		}
//		return abort_t::NO_ABORT;
	}
	
	void print(std::string prefix="") {
		extern TopN<MyHypothesis> top;
		Super::print( prefix + std::to_string(top.count(*this)) + "\t" ); // print but prepend my top count
	}
};


// mydata stores the data for the inference model
MyHypothesis::t_data mydata;
// top stores the top hypotheses we have found
TopN<MyHypothesis> top;

////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Rational rules");
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); // set by above macro

	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
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
//		top(h0);
//	}
//		
//	MyHypothesis h0(&grammar);
//	h0 = h0.restart();
//	MCMCChain chain(h0, &mydata, top);
//	tic();
//	chain.run(mcmc_steps,runtime);
//	tic();
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	ParallelTempering samp(h0, &mydata, top, 8, 1000.0);
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