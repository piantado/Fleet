// TODO: We can probably put all of the GRAMMAR_TYPE stuff into 
// 		the grammar class itself -- have it take some types as arguments
//		and then have add take template arguments, which maybe it can
// 		deduce from a lambda etc -- then al the nt_types are just stored
//      internally to the grammar
// 		maybe the grammar can also store a grammar_dispatch function
//		that takes a constexpr string and converts it to an "op" code
//		so that we can use it as a switch?
//		 

// Steps:
// - move FLEET_GRAMMAR_TYPES into grammar as a template argument, and put all that code in too
// - may need to template the grammar so we can use subtypes etc on the constructor
// - define a constexpr way to conver strings into customOps stored internally in grammar 
//		this function is getOp and will convert a prefix of format into the internal customOp it uses

//- What if we put format and function call (as Operator) together in a struct?







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

#define FLEET_GRAMMAR_TYPES bool,Object,int



#include <string>
#include <cstdlib>
#include <functional>


// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

// or we could define a primitive class
// and each primtiive we create could get a new id, for a new operator
// and could support a call either in VMS or somewhere else

struct PrePrimitive {
	// need to define something all Primtiives inherit from so that
	// we can share op_counters across them	
	static size_t op_counter; // this gives each primitive we define a unique identifier	
};
size_t PrePrimitive::op_counter = 0;
	
// TODO: Make primtiive detect whether T is a VMS or not
// so we can give it a lambda of VMS if we wanted
template<typename T, typename... args> // function type
struct Primitive : PrePrimitive {
	// A primitive associates a string name (format) with a function, 
	// and allows grammars to extract all the relevant function pieces via constexpr,
	// and also defines a VMS function that can be called in dispatch
	// op here is generated uniquely for each Primitive, which is how LOTHypothesis 
	// knows which to call. 
	// SO: grammars use this op (statically updated) in nodes, and then when they 
	// are linearized the op is processed in a switch statement to evaluate
	
	std::string format;
	T(*call)(args...); //call)(Args...);
	size_t op;
	
	Primitive(std::string fmt, T(*f)(args...) ) :
		format(fmt), call(f), op(op_counter++) {
	}
	

	template<typename V>
	void VMScall(V& vms) {
		// a way of calling from a VMS if we want it
		
		// Can't expand liek this because its only for arguments, not 
		assert(not vms._stacks_empty<args>()); // TODO: Implement this... -- need to define empty for variadic TYPES, not arguments...
//		
//		auto ret = this->call(vms.getpop<Args>(), ...);
//		
//		vms.push(ret);
		
		
		// TODO: NEED TO POP TOO DUMMY 
		
	}
	
};




// NO -- we need to use a constexpr counter because it can't be the same across types

//bool theand(bool a, bool b) { return a&&b; }
//Primitive my_and("and(%s,%s)",theand);
//
//Primitive my_and("and(%s,%s)", [](bool a, bool b) -> bool { return a && b; });


// TODO: Must get rid of bools here...
Primitive<bool,bool,bool> my_and("and(%s,%s)", [](bool a, bool b) -> bool { return a && b; });

//Primitive my_or("or(%s,%s)",  [](bool a, bool b) -> bool { return a||b; } );
//Primitive my_not("not(%s)",    [](bool a)         -> bool { return not a; } );



// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add<Object>     (BuiltinOp::op_X,         "x",            1.0);		
		add<bool,Object>(CustomOp::op_Red,        "red(%s)",      1.0);		
		add<bool,Object>(CustomOp::op_Green,      "green(%s)",      1.0);		
		add<bool,Object>(CustomOp::op_Blue,       "blue(%s)",      1.0);		

		add<bool,Object>(CustomOp::op_Square,     "square(%s)",      1.0);		
		add<bool,Object>(CustomOp::op_Triangle,   "triangle(%s)",      1.0);		
		add<bool,Object>(CustomOp::op_Circle,     "circle(%s)",      1.0);		
		
		add(my_and, 1.0);
		//add<bool,bool,bool>(CustomOp::op_And,        "and(%s,%s)",      1.0);		
		add<bool,bool,bool>(CustomOp::op_Or,        "or(%s,%s)",      1.0);		
		add<bool,bool>     (CustomOp::op_Not,        "not(%s)",      1.0);		
	}
};


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
		switch(i.getCustom()) {
			
//			CASE(my_and)
			
			case my_and.op:	my_and.VMScall(vms); break;
			// We want a syntax like:
			// case grammar.getOp("and"): return my_and(x,y)
			// CASE_FUNC("and", my_and)
			
			// Maybe syntax like CASEFUNC1
			// CASE(my_and) // extract everything from the Primitive class
			
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
	// declare a grammar
	//------------------
	MyGrammar grammar;
	
	CERR my_and.op ENDL;
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