// - update description of primitives, instructions, etc. 

// A problem here is that its hard to manage Primitives and BuiltinOps

// TOOD: What if we store a std::vector of rules, which allowed for heterogeneous function types
// and then instruction i could index into the grammar to pull out the rule+function that we needed?
// But part of the problem is that we need to define everything at compile time too 
//
//
// OR Can we just add a std::function list that stores them all??


// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second

#include "assert.h"

enum    class  Shape { Square, Triangle, Circle};
enum    class  Color { Red, Green, Blue};
typedef struct Object {
	Color color;
	Shape shape;
	
	// we must define this to compile because memoization requires sorting (but its only neded in op_MEM)
	bool operator<(const Object& o) const { assert(false); }
} Object;

// These define all of the types that are used in the grammar -- must be defined
// before we import Fleet
#define FLEET_GRAMMAR_TYPES bool,Object

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return a && b; }, 2.0),
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return a || b; }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return not a; }),

	Primitive("red(%s)",       +[](Object x)       -> bool { return x.color == Color::Red; }),
	Primitive("green(%s)",     +[](Object x)       -> bool { return x.color == Color::Green; }),
	Primitive("blue(%s)",      +[](Object x)       -> bool { return x.color == Color::Blue; }),

	Primitive("square(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Square; }),
	Primitive("triangle(%s)",  +[](Object x)       -> bool { return x.shape == Shape::Triangle; }),
	Primitive("circle(%s)",    +[](Object x)       -> bool { return x.shape == Shape::Circle; })
};
// that + is really insane, but is needed to convert a lambda to a function pointer

//template<typename... args>
//std::vector make_primitives(std::tuple<args> tup) {
//}


//typedef t_abort(VirtualMachinePool<Object, bool>&) V;
std::vector primitive_functions {
	static_cast<int>(&std::get<0>(PRIMITIVES).VMScall),
	static_cast<int>(&std::get<0>(PRIMITIVES).VMScall)
	//,
	//std::get<1>(PRIMITIVES).VMScall,
	//std::get<2>(PRIMITIVES).VMScall
};

//template<size_t... Is>
//struct PrimitiveLookup {
//	
//	PrimitiveLookup() 
//	{}
//	
//	PrimitiveLookup(std::index_sequence<Is...>)
//	{}
//	
//	// define pointers to each of their lookups
//	static constexpr void* lookup[] = { (void*)&std::get<Is>(PRIMITIVES).VMScall... };
//};
//
//PrimitiveLookup PL(std::make_index_sequence< std::tuple_size<PRIMITIVES>::value >{});

//template<int N>
//void tuple_call() {
//	
//}

///// NOW Pass this to Grammar and build it in to 
Grammar grammar(PRIMITIVES);


#define PRIMITIVE_CASE(n) case n: return std::get<n>(PRIMITIVES).VMScall(vms);

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
		
//		PRIMITIVES[op].VMScall(vms);
		switch(i.getCustom()) {
//			case 0: return std::get<0>(PRIMITIVES).VMScall(vms);
			PRIMITIVE_CASE(0)
			PRIMITIVE_CASE(1)
			PRIMITIVE_CASE(2)
			PRIMITIVE_CASE(3)
			PRIMITIVE_CASE(4)
			PRIMITIVE_CASE(5)
			PRIMITIVE_CASE(6)
			PRIMITIVE_CASE(7)
			PRIMITIVE_CASE(8)
			
			default:
				assert(0 && " *** You used an invalid operation"); // should never get here
		}
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
	// Include our Built-ins
	//------------------
	
	grammar.add(Rule(grammar.nt<Object>(), BuiltinOp::op_X, "x", {}, 5.0));
	
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