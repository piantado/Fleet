

///########################################################################################
// This is a different formulation of rational rules that uses std::function to allow 
// primitives that manipulate functions (and thus quantifiers like forall, exists)
// To make these useful, we need to include sets of nodes too.
// This also illustrates throwing exceptions in primitives.  
///########################################################################################

// TODO: 
//  Add equal-shape, equal-color operations
// 	Is there a way to make and/or short-circuit?

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the object features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <set>
#include <functional>

#include "Object.h"

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};
//enum class Size  { Small, Medium, Large}

// Define an object with these features
using MyObject = Object<Shape,Color>;

// Define a set of objects
using ObjectSet = std::set<MyObject>;

// Declare function types
using ObjectToBool        = std::function<bool(MyObject)>;
using ObjectxObjectToBool = std::function<bool(MyObject,MyObject)> ;

// The arguments to a hypothesis will be a pair of a set and an object (as in Piantadosi, Tenenbaum, Goodman 2016)
using ArgType = std::tuple<MyObject,ObjectSet>; // this is the type of arguments we give to our function

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
	Primitive("red",       +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Red);}; }),
	Primitive("green",     +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Green);}; }),
	Primitive("blue",      +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Blue);}; }),

	Primitive("square",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Square);}; }),
	Primitive("triangle",  +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Triangle);}; }),
	Primitive("circle",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Circle);}; }),
	
	// Define logical operators as operating over the functions. These combine functions from MyObject->Bool into 
	// new ones from MyObject->Bool. Note that these inner lambdas must capture by copy, not reference, since when we 
	// call them, a and b may not be around anymore. We've written them with [] to remind us their arguments are functions, not objects
	// NOTE: These do not short circuit...
	Primitive("and[%s,%s]",   +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return a(x) and b(x);}; }, 2.0), // optional specification of prior weight (default=1.0)
	Primitive("or[%s,%s]",    +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return a(x) or b(x);}; }), 
	Primitive("not[%s]",      +[](ObjectToBool a)                 -> ObjectToBool { return [=](MyObject x) { return not a(x);}; }), 
	
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
	
	Primitive("contains(%s, %s)",   +[](MyObject z, ObjectSet s)    -> bool { 
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

	Primitive("iota(%s)",   +[](ObjectSet s)    -> MyObject { 
		// return the unique element in the set. If not, we throw an exception
		// which must be caught in calling below. 
		if(s.size() > 1) throw IotaException();
		else             return *s.begin();
	}), 
	
	// Relations -- these will require currying
	Primitive("same-shape",   +[]() -> ObjectxObjectToBool { 
		return +[](MyObject x, MyObject y) { return x.get<Shape>() == y.get<Shape>();}; 
	}),
	
	Primitive("same-color",   +[]() -> ObjectxObjectToBool { 
		return +[](MyObject x, MyObject y) { return x.get<Color>() == y.get<Color>();}; 
	}),
	
	Primitive("curry[%s,%s]",   +[](ObjectxObjectToBool f, MyObject x) -> ObjectToBool { 
		return [f,x](MyObject y) { return f(x,y); }; 
	}),
	
	// add an application operator
	Primitive("apply(%s,%s)",       +[](ObjectToBool f, MyObject x)  -> bool { return f(x); }, 10.0),
	
	// And we assume that we're passed a tuple of an object and set
	Primitive("%s.o",       +[](ArgType t)  -> MyObject    { return std::get<0>(t); }),
	Primitive("%s.s",       +[](ArgType t)  -> ObjectSet { return std::get<1>(t); }),
	
	Builtin::X<ArgType>("x")
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,Object)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

using MyGrammar = Grammar<bool,MyObject,ArgType,ObjectSet,ObjectToBool,ObjectxObjectToBool>;

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
	TopN<MyHypothesis> top;
	
	// Define the grammar (default initialize using our primitives will add all those rules)
	// in doing this, grammar deduces the types from the input and output types of each primitive
	MyGrammar grammar(PRIMITIVES);
	
	//------------------
	// set up the data
	//------------------
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;	
	
	ObjectSet s = {MyObject(Shape::Triangle, Color::Red),
				   MyObject(Shape::Square, Color::Green),
				   MyObject(Shape::Triangle, Color::Red)};
	MyObject x = MyObject(Shape::Triangle, Color::Red);
	
	mydata.push_back(MyHypothesis::datum_t{.input=std::make_tuple(x,s), .output=true,  .reliability=0.99});
	
	//------------------
	// Actually run
	//------------------
		
	auto h0 = MyHypothesis::make(&grammar);
	ParallelTempering samp(h0, &mydata, top, 16, 10.0); 
	samp.run(Control(), 100, 1000); 		
	
	// Show the best we've found
	top.print();
}