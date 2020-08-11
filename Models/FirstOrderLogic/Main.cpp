

///########################################################################################
// This is a differnet formulation of rational rules that uses std::function to allow 
// primitives that manipulate functions (and thus quantifiers like forall, exists)
// To make these useful, we need to include sets of nodes too.
// This also illustrates throwing exceptions in primitives.  
///########################################################################################


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the object features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <set>
#include <functional>

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};

struct Object { 
	Color color; 
	Shape shape; 
	
	// Must define operator< in order to be stored in a set
	bool operator<(const Object& o) const {
		if( color != o.color) return color < o.color;
		else                  return shape < o.shape;
	}
	
	bool operator==(const Object o) const {
		return (shape==o.shape) and (color==o.color);
	}
};


// Define a set of objects
typedef std::set<Object> ObjectSet;

/**
 * @class ObjectToBool
 * @brief Declare a function type so that we can construct with a pointer
 */
struct ObjectToBool : public std::function<bool(Object)> {
	using F = std::function<bool(Object)>;
	using F::F;
	ObjectToBool( bool* f(Object) ) {
		*this = f;
	}
};

// The arguments to a hypothesis will be a pair of a set and an object (as in Piantadosi, Tenenbaum, Goodman 2016)
typedef std::tuple<Object,ObjectSet> ArgType; // this is the type of arguments we give to our function

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

#include <exception>

class IotaException : public std::exception {};

std::tuple PRIMITIVES = {

	// these are funny primitives that to fleet are functions with no arguments, but themselves
	// return functions from objects to bool (ObjectToBool):
	Primitive("red",       +[]() -> ObjectToBool { return +[](Object x) {return x.color == Color::Red;}; }),
	Primitive("green",     +[]() -> ObjectToBool { return +[](Object x) {return x.color == Color::Green;}; }),
	Primitive("blue",      +[]() -> ObjectToBool { return +[](Object x) {return x.color == Color::Blue;}; }),

	Primitive("square",    +[]() -> ObjectToBool { return +[](Object x) {return x.shape == Shape::Square;}; }),
	Primitive("triangle",  +[]() -> ObjectToBool { return +[](Object x) {return x.shape == Shape::Triangle;}; }),
	Primitive("circle",    +[]() -> ObjectToBool { return +[](Object x) {return x.shape == Shape::Circle;}; }),
	
	// Define logical operators as operating over the functions. These combine functions from Object->Bool into 
	// new ones from Object->Bool. Note that these inner lambdas must capture by copy, not reference, since when we 
	// call them, a and b may not be around anymore. We've written them with [] to remind us their arguments are functions, not objects
	Primitive("and[%s,%s]",   +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](Object x) { return a(x) and b(x);}; }, 2.0), // optional specification of prior weight (default=1.0)
	Primitive("or[%s,%s]",    +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](Object x) { return a(x) or b(x);}; }), 
	Primitive("not[%s]",      +[](ObjectToBool a)                 -> ObjectToBool { return [=](Object x) { return not a(x);}; }), 
	
	Primitive("forall(%s,%s)",   +[](ObjectToBool f, ObjectSet s)    -> bool { 
		for(auto& x : s) {
			if(not f(x)) return false;
		}
		return true;
	}), 
	
	Primitive("exists(%s, %s)",   +[](ObjectToBool f, ObjectSet s)    -> bool { 
		for(auto& x : s) {
			if(f(x)) return true;
		}
		return false;
	}), 
	
	Primitive("contains(%s, %s)",   +[](Object z, ObjectSet s)    -> bool { 
		for(auto& x : s) {
			if(z == x) return true;
		}
		return false;
	}), 
	
	Primitive("filter(%s, %s)",   +[](ObjectSet s, ObjectToBool f)    -> ObjectSet { 
		ObjectSet out;
		for(auto& x : s) {
			if(f(x)) out.insert(x);
		}
		return out;
	}), 
	
	Primitive("empty(%s)",   +[](ObjectSet s)    -> bool { 
		return s.empty();
	}), 

	
	Primitive("iota(%s)",   +[](ObjectSet s)    -> Object { 
		// return the unique element in the set. If not, we throw an exception
		// which must be caught in calling below. 
		if(s.size() > 1) throw IotaException();
		else             return *s.begin();
	}), 
	
	
	// add an application operator
	Primitive("apply(%s,%s)",       +[](ObjectToBool f, Object x)  -> bool { return f(x); }, 10.0),
	
	// And we assume that we're passed a tuple of an object and set
	Primitive("%s.o",       +[](ArgType t)  -> Object    { return std::get<0>(t); }),
	Primitive("%s.s",       +[](ArgType t)  -> ObjectSet { return std::get<1>(t); }),
	
	Builtin::X<ArgType>("x")
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,Object)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,Object,ArgType,ObjectSet,ObjectToBool> {
	using Super =  Grammar<bool,Object,ArgType,ObjectSet,ObjectToBool>;
	using Super::Super;
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,ArgType,bool,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,ArgType,bool,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability x.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& x) override {
		bool out = false;
		
		try { 
			
			out = callOne(x.input, false);
			
		} catch (IotaException& e) { 
			// we could default (e.g. to false with nothing here) 
			// or we could want to avoid ever calling iota unless it works
			//return -infinity; 
		} 
		
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
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
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top(ntop);//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)
	// in doing this, grammar deduces the types from the input and output types of each primitive
	MyGrammar grammar(PRIMITIVES);
	
	//------------------
	// set up the data
	//------------------
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;	
	
	ObjectSet s = {Object{.color=Color::Red, .shape=Shape::Triangle},
				   Object{.color=Color::Red, .shape=Shape::Triangle},
				   Object{.color=Color::Red, .shape=Shape::Triangle}};
	Object x = Object{.color=Color::Red, .shape=Shape::Triangle};
	
	mydata.push_back(MyHypothesis::datum_t{.input=std::make_tuple(x,s), .output=true,  .reliability=0.99});
	
	//------------------
	// Actually run
	//------------------
		
	auto h0 = MyHypothesis::make(&grammar);
	ParallelTempering samp(h0, &mydata, top, 16, 10.0); 
	tic();
	samp.run(Control(mcmc_steps,runtime,nthreads), 100, 1000); 		
	tic();

	// Show the best we've found
	top.print();
}