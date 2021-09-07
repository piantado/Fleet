

///########################################################################################
// This is a different formulation of rational rules that uses std::function to allow 
// primitives that manipulate functions (and thus quantifiers like forall, exists)
// To make these useful, we need to include sets of nodes too.
// This also illustrates throwing exceptions in primitives.  
///########################################################################################

#include <functional>
#include <cassert>

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the object features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Object.h"

using S = std::string;

#include "ShapeColorSizeObject.h"

using MyObject = ShapeColorSizeObject; 

// Define a set of objects -- really it's a multiset so we'll use a vector
using ObjectSet = std::vector<MyObject>;

// Declare function types
using ObjectToBool        = std::function<bool(MyObject)>;
using ObjectxObjectToBool = std::function<bool(MyObject,MyObject)> ;

// The arguments to a hypothesis will be a pair of a set and an object (as in Piantadosi, Tenenbaum, Goodman 2016)
using MyInput = std::tuple<MyObject,ObjectSet>; // this is the type of arguments we give to our function

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,Object)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<MyInput,bool,   
						         bool,MyObject,MyInput,ObjectSet,ObjectToBool,ObjectxObjectToBool>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		this->add("true",    +[]() -> bool { return true; }),
		this->add("false",   +[]() -> bool { return false; }),
		
		// these are funny primitives that to fleet are functions with no arguments, but themselves
		// return functions from objects to bool (ObjectToBool):
		this->add("yellow",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Yellow);}; }),
		this->add("green",     +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Green);}; }),
		this->add("blue",      +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Color::Blue);}; }),

		this->add("rectangle", +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Rectangle);}; }),
		this->add("triangle",  +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Triangle);}; }),
		this->add("circle",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Shape::Circle);}; }),
		
		this->add("size1",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Size::size1);}; }),
		this->add("size2",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Size::size2);}; }),
		this->add("size3",    +[]() -> ObjectToBool { return +[](MyObject x) {return x.is(Size::size3);}; }),
		
		// Define logical operators as operating over the functions. These combine functions from MyObject->Bool into 
		// new ones from MyObject->Bool. Note that these inner lambdas must capture by copy, not reference, since when we 
		// call them, a and b may not be around anymore. We've written them with [] to remind us their arguments are functions, not objects
		// NOTE: These do not short circuit...
		this->add("and[%s,%s]",   +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return a(x) and b(x);}; }, 2.0), // optional specification of prior weight (default=1.0)
		this->add("or[%s,%s]",    +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return a(x) or b(x);}; }), 
		this->add("not[%s]",      +[](ObjectToBool a)                 -> ObjectToBool { return [=](MyObject x) { return not a(x);}; }), 
		
		this->add("nand[%s,%s]",      +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return not(a(x) and b(x));}; }, .25),
		this->add("nor[%s,%s]",       +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return not(a(x) or b(x));}; }, .25), 
		this->add("iff[%s,%s]",       +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return a(x) == b(x);}; }, .25), 
		this->add("implies[%s,%s]",   +[](ObjectToBool a, ObjectToBool b) -> ObjectToBool { return [=](MyObject x) { return (not a(x)) or b(x);};}, .25 ), 
		
		this->add("forall(%s,%s)",   +[](ObjectToBool f, ObjectSet s)    -> bool { 
			for(auto& x : s) {
				if(not f(x)) return false;
			}
			return true;
		}), 
		
		this->add("exists(%s, %s)",   +[](ObjectToBool f, ObjectSet s)    -> bool { 
			for(auto& x : s) {
				if(f(x)) return true;
			}
			return false;
		}), 
		
		this->add("contains(%s, %s)",   +[](MyObject z, ObjectSet s)    -> bool { 
			for(auto& x : s) {
				if(z == x) return true;
			}
			return false;
		}), 
		
		this->add("filter(%s, %s)",   +[](ObjectSet s, ObjectToBool f)    -> ObjectSet { 
			ObjectSet out;
			for(auto& x : s) {
				if(f(x)) out.push_back(x);
			}
			return out;
		}), 
		
		this->add("empty(%s)",   +[](ObjectSet s)    -> bool { 
			return s.empty();
		}), 

		this->add("iota(%s)",   +[](ObjectSet s)    -> MyObject { 
			// return the unique element in the set. If not, we throw an exception
			// which must be caught in calling below. 
			if(s.size() != 1) throw VMSRuntimeError();
			else             return *s.begin();
		}), 
		
		// Relations -- these will require currying
		this->add("same_shape",   +[]() -> ObjectxObjectToBool { 
			return +[](MyObject x, MyObject y) { return x.get<Shape>() == y.get<Shape>();}; 
		}),
		
		this->add("same_color",   +[]() -> ObjectxObjectToBool { 
			return +[](MyObject x, MyObject y) { return x.get<Color>() == y.get<Color>();}; 
		}),
		
		this->add("same_size",   +[]() -> ObjectxObjectToBool { 
			return +[](MyObject x, MyObject y) { return x.get<Size>() == y.get<Size>();}; 
		}),
		
		this->add("size-lt",   +[]() -> ObjectxObjectToBool { 
			// we can cast to int to compare gt size
			return +[](MyObject x, MyObject y) { return (int)x.get<Size>() < (int)y.get<Size>();}; 
		}, 0.25),
		
		this->add("size-leq",   +[]() -> ObjectxObjectToBool { 
			// we can cast to int to compare gt size
			return +[](MyObject x, MyObject y) { return (int)x.get<Size>() <= (int)y.get<Size>();}; 
		}, 0.25),
		
		// Since we only curry by taking the first arg, we include both orders here (and give them low priors)
		this->add("size-gt",   +[]() -> ObjectxObjectToBool { 
			// we can cast to int to compare gt size
			return +[](MyObject x, MyObject y) { return (int)x.get<Size>() > (int)y.get<Size>();}; 
		}, 0.25),
		
		this->add("size-geq",   +[]() -> ObjectxObjectToBool { 
			// we can cast to int to compare gt size
			return +[](MyObject x, MyObject y) { return (int)x.get<Size>() >= (int)y.get<Size>();}; 
		}, 0.25),
		
		this->add("apply(%s,%s)",   +[](ObjectxObjectToBool f, MyObject x) -> ObjectToBool { 
			return [f,x](MyObject y) { return f(x,y); }; 
		}),
		
		// add an application operator
		this->add("apply(%s,%s)",       +[](ObjectToBool f, MyObject x)  -> bool { return f(x); }, 10.0),
		
		// And we assume that we're passed a tuple of an object and set
		this->add("%s.o",       +[](MyInput t)  -> MyObject  { return std::get<0>(t); }),
		this->add("%s.s",       +[](MyInput t)  -> ObjectSet { return std::get<1>(t); }),
		this->add("x",          Builtins::X<MyGrammar>);
		
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyInput,bool,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,MyInput,bool,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability x.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& x) override {
		bool out = callOne(x.input, false);			
		
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
	}
	
	
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// we can not include main by defining this
#ifndef NO_FOL_MAIN

#include "TopN.h"
#include "ParallelTempering.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("First order logic example");
	fleet.initialize(argc, argv);

	//------------------
	// set up the data
	//------------------
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;	
	
	ObjectSet s = {MyObject(Shape::Triangle, Color::Blue, Size::size1),
				   MyObject(Shape::Rectangle, Color::Green, Size::size2),
				   MyObject(Shape::Triangle, Color::Blue, Size::size3)};
	MyObject x = MyObject(Shape::Triangle, Color::Blue, Size::size1);
	
	mydata.push_back(MyHypothesis::datum_t{.input=std::make_tuple(x,s), .output=true,  .reliability=0.99});
	
	//------------------
	// Actually run
	//------------------
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top;
	
	auto h0 = MyHypothesis::sample();
	ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 10.0); 
	for(auto& h : samp.run(Control())) { 
		top << h;
	}
	
	// Show the best we've found
	top.print();
}

#endif