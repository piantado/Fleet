#include <cmath>

// TODO: Add min/max?

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

const double reliability = 0.99;
const int N = 100; // what number do we go up to?
const int Nlow = 1; // min -- 1 or 0, who can ever remember?

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <set>
#include "Strings.h"

#include "NumberSet.h"

// first we're going to declare some sets that are relevant
const NumberSet numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100};
const NumberSet primes  = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};
const NumberSet evens   = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100};
const NumberSet odds    = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91, 93, 95, 97, 99};
const NumberSet squares = {1, 4, 9, 16, 25, 36, 49, 64, 81, 100};
const NumberSet decades = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

// just for fun here
const NumberSet elevens = {11, 22, 33, 44, 55, 66, 77, 88, 99};

// probably should expect to be low:
const NumberSet fibonacci = {1, 2, 3, 5, 8, 13, 21, 34, 55, 89};
const NumberSet weird = {7, 44, 89}; // just some arbitrary set -- probably low prior in grammar inference


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<int, NumberSet,    int, NumberSet>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		add("numbers",    +[]() -> NumberSet { return numbers; });
		add("primes",     +[]() -> NumberSet { return primes; });
		add("evens",      +[]() -> NumberSet { return evens; });
		add("odds",       +[]() -> NumberSet { return odds; });
		add("squares",    +[]() -> NumberSet { return squares; });
		add("decades",    +[]() -> NumberSet { return decades; });
		add("elevens",    +[]() -> NumberSet { return elevens; });
		add("fibonacci",  +[]() -> NumberSet { return fibonacci; });
		add("weird",      +[]() -> NumberSet { return weird; });
		
		// we give range a very low prior here or else it sure dominates
		add("range(%s,%s)",  +[](int x, int y) -> NumberSet { 
			NumberSet out;
			for(int i=std::max(x,Nlow);i<=std::min(y,N);i++) { // we need min/max bounds here or we spend all our time adding garbage
				out.insert(i);
			}
			return out;
		}, 0.1);
		
		add("union(%s,%s)",    +[](NumberSet a, NumberSet b) -> NumberSet { 
			for(auto& x : b) {
				a.insert(x);
			}
			return a;
		});
		
		add("intersection(%s,%s)",    +[](NumberSet a, NumberSet b) -> NumberSet { 
			NumberSet out;
			for(auto& x : a) {
				if(b.find(x) != b.end())
					out.insert(x);
			}
			return out;
		});


		add("complement(%s)",    +[](NumberSet a) -> NumberSet { 
			NumberSet out;
			for(size_t i=Nlow;i<N;i++) {
				if(a.find(i) == a.end())
					out.insert(i);
			}
			return out;
		});

		
		add("(%s+%s)",    +[](NumberSet s, int n) -> NumberSet { 
			NumberSet out;
			for(auto& x : s) {
				out.insert(x + n);
			}
			return out;
		});
		
		add("(%s*%s)",    +[](NumberSet s, int n) -> NumberSet { 
			NumberSet out;
			for(auto& x : s) {
				out.insert(x * n);
			}
			return out;
		});
		
		add("(%s^%s)",    +[](NumberSet s, int n) -> NumberSet { 
			NumberSet out;
			for(auto& x : s) {
				out.insert(std::pow(x,n));
			}
			return out;
		});	
		add("(%s^%s)",    +[](int n, NumberSet s) -> NumberSet { 
			NumberSet out;
			for(auto& x : s) {
				out.insert(std::pow(n,x));
			}
			return out;
		});
		
		// int operations
		add("(%s+%s)",    +[](int x, int y) -> int { return x+y;});
		add("(%s*%s)",    +[](int x, int y) -> int { return x*y;});
		add("(%s-%s)",    +[](int x, int y) -> int { return x-y;});
		add("(%s^%s)",    +[](int x, int y) -> int { return std::pow(x,y);});
		
		for(int i=Nlow;i<=N;i++) {
			// We put "#" here so that in grammar inference we can find these rules easily!
			add_terminal( "#"+str(i), i, 10.0/(N-Nlow));
		}
		
		add("x",             Builtins::X<MyGrammar>);
		
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
/// This is an example where our data is different -- it's a set instead of a vector
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include<set>
#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,int,NumberSet,MyGrammar,&grammar,std::multiset<int> > {
public:
	using Super = LOTHypothesis<MyHypothesis,int,NumberSet,MyGrammar,&grammar,std::multiset<int> >;
	using Super::Super; // inherit the constructors
	
	// Ok so technically since we don't take any args, this will callOne for each data point
	// but it shouldn't matter because all of our data is of length 1. 
	// NOTE: This is needed for GrammarHypothesis, so we can't just overwrite compute_likelihood
	virtual double compute_single_likelihood(const datum_t& datum) override {
		NumberSet out = callOne(1); // just give x=1
		double sz = out.size();
		
		double ll = 0.0;
		for(auto& x : datum) {
			ll += log((out.find(x)!=out.end() ? reliability / sz : 0.0)  +
							  (1.0-reliability)/N);
		}
		return ll;
	}

};

////////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////////

#ifndef NUMBER_GAME_DO_NOT_INCLUDE_MAIN

#include "Fleet.h"
#include "TopN.h"
#include "MCMCChain.h"

int main(int argc, char** argv){ 	
	Fleet fleet("Number Game");
	fleet.initialize(argc, argv);

	// Define something to hold the best hypotheses
	TopN<MyHypothesis> top;

	// our data
	MyHypothesis::data_t mydata = { std::multiset<int>{2,4,32} };	

	// and sample with just one chain
	auto h0 = MyHypothesis::sample();
	MCMCChain samp(h0, &mydata);
	for(auto& h : samp.run(Control())) {
		top << h;
	}

	// print the results
	top.print();
}

#endif