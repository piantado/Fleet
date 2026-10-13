

///########################################################################################
// This is a different formulation of rational rules that uses std::function to allow 
// primitives that manipulate functions (and thus quantifiers like forall, exists)
// To make these useful, we need to include sets of nodes too.
// This also illustrates throwing exceptions in primitives.  
///########################################################################################

#include "Functional.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the object features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Object.h"

using S = std::string;

#include "ShapeColorSizeObject.h"

using Obj = ShapeColorSizeObject; 

// Define a set of objects -- really it's a multiset so we'll use a vector
using Set = std::vector<Obj>;

using Bset = std::vector<bool>; // one boolean response for each element of a set

struct GrammarInput {
	Obj o;
	Set s;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"
#include "Singleton.h"

double TERMINAL_WEIGHT = 5.0;

// The grammar here descibes functions that takes a set and object and returns a bool
// NOTE: hypotheses don't do this -- they map over everything

class MyGrammar : public Grammar<GrammarInput,bool,   
						         bool,Obj,GrammarInput,Set,
								 ft<bool,bool>, ft<bool,bool,bool>,
								 ft<bool,Obj>, ft<bool,Obj,Obj>, 
								 ft<bool,bool,Obj>
								 >,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {	

		using namespace std::placeholders;  // for _1, _2, etc. in bind

		// we are going to make the type system a little cleaner -- here we use
		// b and o only within the function type ft -- so something that returns a bool from an object and a bool is ft<b,o,b>
		using b = bool; 
		using o = Obj;

		add("true",    +[]() -> bool { return true; }, 0.05); // give these low prior so we don't spend much time proposing them
		add("false",   +[]() -> bool { return false; }, 0.05);
		
		// these are funny primitives that to fleet are functions with no arguments, but themselves
		// return functions from objects to bool (ObjectToBool):
		add("yellow",    +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Color::Yellow);}; });
		add("green",     +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Color::Green);}; });
		add("blue",      +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Color::Blue);}; });

		add("rectangle", +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Shape::Rectangle);}; });
		add("triangle",  +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Shape::Triangle);}; });
		add("circle",    +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Shape::Circle);}; });
		
		add("small",    +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Size::size1);}; });
		add("medium",   +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Size::size2);}; });
		add("large",    +[]() -> ft<b,o> { return +[](Obj x) {return x.is(Size::size3);}; });
		
		// Define logical operators as operating over the functions. These combine functions from Obj->Bool into 
		// new ones from Obj->Bool. Note that these inner lambdas must capture by copy, not reference, since when we 
		// call them, a and b may not be around anymore. We've written them with [] to remind us their arguments are functions, not objects
		// NOTE: These do not short circuit...
		add("and",     +[]() -> ft<b,b,b> { return +[](bool x, bool y) -> bool { return (x and y); };});
		add("or",      +[]() -> ft<b,b,b> { return +[](bool x, bool y) -> bool { return (x or y); };});
		add("not",     +[]() -> ft<b,b>   { return +[](bool x)         -> bool { return (not x); };});
		add("iff",     +[]() -> ft<b,b,b> { return +[](bool x, bool y) -> bool { return (x == y); };});
		add("implies", +[]() -> ft<b,b,b> { return +[](bool x, bool y) -> bool { return ((not x) or y); };});
		
		// and define our composition operations -- these capture partial application in all orders
		add("apply1(%s,%s)", +[](ft<b,o> f,   Obj x) -> b       { return f(x); }, 5.0);
		add("apply2(%s,%s)", +[](ft<b,o,o> f, Obj x) -> ft<b,o> { return std::bind(f, _1, x);} );
		add("apply3(%s,%s)", +[](ft<b,o,o> f, Obj x) -> ft<b,o> { return std::bind(f, x, _1);} );
		add("apply4(%s,%s)", +[](ft<b,o,o> f, Obj x) -> b       { return f(x,x);} ); // bind in both places
		add("apply5(%s,%s)", +[](ft<b,b,o> f, Obj x) -> ft<b,b> { return std::bind(f, _1, x);} );
		add("apply6(%s,%s)", +[](ft<b,b,o> f, b x)        -> ft<b,o> { return std::bind(f, x, _1);} ); // won't bind to second argument
	
		add("apply7(%s,%s)", +[](ft<b,b> f,   b x) -> b { return f(x); });
		add("apply8(%s,%s)", +[](ft<b,b,b> f, b x) -> ft<b,b> { return std::bind(f, x, _1);} );
		add("apply9(%s,%s)", +[](ft<b,b,b> f, b x) -> ft<b,b> { return std::bind(f, _1, x);} );
		
		// and some composition operations
		add("compose1(%s,%s)", +[](ft<b,b> f, ft<b,b> g)   -> ft<b,b>   { return compose(f,g); });
		add("compose2(%s,%s)", +[](ft<b,b> f, ft<b,o> g)   -> ft<b,o>   { return compose(f,g); });
		add("compose3(%s,%s)", +[](ft<b,b> f, ft<b,o,o> g) -> ft<b,o,o> { return compose(f,g); });
		
		add("compose4(%s,%s)", +[](ft<b,b,b> f, ft<b,b> g)     -> ft<b,b,b>  { 
			return [=](bool x, bool y) { return f(g(x),y); }; 
		});
		add("compose5(%s,%s)", +[](ft<b,b,b> f, ft<b,b> g)     -> ft<b,b,b>  { 
			return [=](bool x, bool y) { return f(x,g(y)); }; 
		});
		add("compose6(%s,%s)", +[](ft<b,b,b> f, ft<b,b> g)     -> ft<b,b,b>  { 
			return [=](bool x, bool y) { return f(g(x),g(y)); }; 
		});

		add("compose7(%s,%s)", +[](ft<b,b,b> f, ft<b,o> g)     -> ft<b,b,o>  { 
			return [=](bool y, Obj x) { return f(g(x),y); }; 
		});
		add("compose8(%s,%s)", +[](ft<b,b,b> f, ft<b,o> g)     -> ft<b,b,o>  { 
			return [=](bool y, Obj x) { return f(y,g(x)); };
		});
		// applying b to both values of a probably doe not make sense...
		
		add("compose9(%s,%s)", +[](ft<b,b,o> f, ft<b,o> g) -> ft<b,o,o> { 
			return [=](Obj x, Obj y) -> bool { return f(g(x),y); }; 
		});
		add("compose10(%s,%s)", +[](ft<b,b,o> f, ft<b,o> g) -> ft<b,o,o> { 
			return [=](Obj x, Obj y) -> bool { return f(g(y),x); }; 
		});
		add("compose11(%s,%s)", +[](ft<b,b,o> f, ft<b,o> g) -> ft<b,o> { 
			return [=](Obj x) -> bool { return f(g(x),x); }; 
		});

		//		
		add("forall(%s,%s)",   +[](ft<b,o> f, Set s)    -> bool { 
			for(auto& x : s) {
				if(not f(x)) return false;
			}
			return true;
		}); 
		
		add("exists(%s, %s)",   +[](ft<b,o> f, Set s)    -> bool { 
			for(auto& x : s) {
				if(f(x)) return true;
			}
			return false;
		}); 
		
		add("contains(%s, %s)",   +[](Obj z, Set s)    -> bool { 
			for(auto& x : s) {
				if(z == x) return true;
			}
			return false;
		}); 
		
		add("filter(%s, %s)",   +[](ft<b,o> f, Set s)    -> Set { 
			Set out;
			for(auto& x : s) {
				if(f(x)) out.push_back(x);
			}
			return out;
		}); 
		
		add("empty(%s)",   +[](Set s)    -> bool { 
			return s.empty();
		}); 

		add("the(%s)",   +[](Set s)    -> Obj { 
			// return the unique element in the set. If not, we throw an exception
			// which must be caught in calling below. 
			if(s.size() != 1) throw VMSRuntimeError();
			else             return *s.begin();
		}); 
		
		// Relations -- these will require currying
		add("same_shape",   +[]() -> ft<b,o,o> { 
			return +[](Obj x, Obj y) { return x.get<Shape>() == y.get<Shape>();}; 
		});
		
		add("same_color",   +[]() -> ft<b,o,o> { 
			return +[](Obj x, Obj y) { return x.get<Color>() == y.get<Color>();}; 
		});
		
		add("same_size",   +[]() -> ft<b,o,o> { 
			return +[](Obj x, Obj y) { return x.get<Size>() == y.get<Size>();}; 
		});
		
		add("size-lt",   +[]() -> ft<b,o,o> { 
			// we can cast to int to compare gt size
			return +[](Obj x, Obj y) { return (int)x.get<Size>() < (int)y.get<Size>();}; 
		});
		
		add("size-leq",   +[]() -> ft<b,o,o> { 
			// we can cast to int to compare gt size
			return +[](Obj x, Obj y) { return (int)x.get<Size>() <= (int)y.get<Size>();}; 
		});
		
			
		// we need to include these because apply is always to first argument
		add("size-gt",   +[]() -> ft<b,o,o> { 
			// we can cast to int to compare gt size
			return +[](Obj x, Obj y) { return (int)x.get<Size>() > (int)y.get<Size>();}; 
		});
		
		add("size-geq",   +[]() -> ft<b,o,o> { 
			// we can cast to int to compare gt size
			return +[](Obj x, Obj y) { return (int)x.get<Size>() >= (int)y.get<Size>();}; 
		});
		
		// And we assume that we're passed a tuple of an object and set
		add("%s.o",       +[](GrammarInput t)  -> Obj  { return t.o; });
		add("%s.s",       +[](GrammarInput t)  -> Set { return t.s; }, TERMINAL_WEIGHT);
		add("x",          Builtins::X<MyGrammar>, TERMINAL_WEIGHT);
		
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "DeterministicLOTHypothesis.h"

// The actual hypothesis we want will output a set, so we need a new type that overrides call
class MyHypothesis : public DeterministicLOTHypothesis<MyHypothesis,
													   Set,BSet,
													   MyGrammar,&grammar> {
														   
	Bset call(const Set& x) {
		auto n = x.size();
		Bset out(n);
		for(size_t i=0;i<n;i++) {
			GrammarInput gi(x.at(i), x);
			out.at(i) = call(gi);
		}
		return out; 
	}
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability x.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& x) override {
		bool out = this->call(x.input);			
		
		double ll = 0.0;
		for(size_t i=0;i<x.s.size();i++) {
			ll += (out.at(i) == x.output.at(i)) ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
		}
		return ll;
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
	std::vector<MyHypothesis::datum_t> mydata;	
	
	Set s = {Obj(Shape::Triangle,  Color::Blue,  Size::size1),
				   Obj(Shape::Rectangle, Color::Green, Size::size2),
				   Obj(Shape::Triangle,  Color::Blue,  Size::size3)};
	Bset b = {true, true, false};
	
	mydata.emplace_back(s,b,0.99);
			
	//------------------
	// Actually run
	//------------------
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top;
	
	auto h0 = MyHypothesis::sample();
	ParallelTempering samp(h0, mydata, FleetArgs::nchains, 10.0); 
	for(auto& h : samp.run(Control()) | top | printer(FleetArgs::print)) { 
		UNUSED(h);
	}
	
	// Show the best we've found
	top.print();
}

#endif