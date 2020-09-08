#include <cmath>

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

const double reliability = 0.99;
const int N = 100; // what number do we go up to?
const int m = 1; // min -- 1 or 0, who can ever remember?

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


#include "Primitives.h"
#include "Builtins.h"


std::tuple PRIMITIVES = {
	Primitive("numbers",    +[]() -> NumberSet { return numbers; }),
	Primitive("primes",     +[]() -> NumberSet { return primes; }),
	Primitive("evens",      +[]() -> NumberSet { return evens; }),
	Primitive("odds",       +[]() -> NumberSet { return odds; }),
	Primitive("squares",    +[]() -> NumberSet { return squares; }),
	Primitive("decades",    +[]() -> NumberSet { return decades; }),
	
	// we give range a very low prior here or else it sure dominates
	Primitive("range(%s,%s)",  +[](int x, int y) -> NumberSet { 
		NumberSet out;
		for(int i=std::max(x,m);i<=std::min(y,N);i++) { // we need min/max bounds here or we spend all our time adding garbage
			out.insert(i);
		}
		return out;
	}, 0.1),
	
	// ADD UNION, INTERSECTION, MIN, MAX
	
	Primitive("union(%s,%s)",    +[](NumberSet& a, NumberSet b) -> void { 
		for(auto& x : b) {
			a.insert(x);
		}
	}),
	
	Primitive("intersection(%s,%s)",    +[](NumberSet a, NumberSet b) -> NumberSet { 
		NumberSet out;
		for(auto& x : a) {
			if(b.find(x) != b.end())
				out.insert(x);
		}
		return out;
	}),


	Primitive("complement(%s)",    +[](NumberSet a) -> NumberSet { 
		NumberSet out;
		for(size_t i=m;i<N;i++) {
			if(a.find(i) == a.end())
				out.insert(i);
		}
		return out;
	}),

	
	Primitive("(%s+%s)",    +[](NumberSet s, int n) -> NumberSet { 
		NumberSet out;
		for(auto& x : s) {
			out.insert(x + n);
		}
		return out;
	}),
	
	Primitive("(%s*%s)",    +[](NumberSet s, int n) -> NumberSet { 
		NumberSet out;
		for(auto& x : s) {
			out.insert(x * n);
		}
		return out;
	}),
	
	Primitive("(%s^%s)",    +[](NumberSet s, int n) -> NumberSet { 
		NumberSet out;
		for(auto& x : s) {
			out.insert(std::pow(x,n));
		}
		return out;
	}),	
	Primitive("(%s^%s)",    +[](int n, NumberSet s) -> NumberSet { 
		NumberSet out;
		for(auto& x : s) {
			out.insert(std::pow(n,x));
		}
		return out;
	}),
	
	// int operations
	Primitive("(%s+%s)",    +[](int x, int y) -> int { return x+y;}),
	Primitive("(%s*%s)",    +[](int x, int y) -> int { return x*y;}),
	Primitive("(%s-%s)",    +[](int x, int y) -> int { return x-y;}),
	Primitive("(%s^%s)",    +[](int x, int y) -> int { return std::pow(x,y);}),
	
	Builtin::X<int>("x", 1.0)
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<NumberSet,int> {
	using Super=Grammar<NumberSet,int>;
	using Super::Super;
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
/// This is an example where our data is different -- it's a set instead of a vector
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include<set>
#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,int,NumberSet,MyGrammar,std::multiset<int> > {
public:
	using Super = LOTHypothesis<MyHypothesis,int,NumberSet,MyGrammar,std::multiset<int> >;
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
#include "Top.h"
#include "MCMCChain.h"

int main(int argc, char** argv){ 	
	Fleet fleet("Number Game");
	fleet.initialize(argc, argv);

	MyGrammar grammar(PRIMITIVES);
	
	for(int i=m;i<=N;i++) {
		grammar.add<int>(BuiltinOp::op_INT, str(i), 10.0/N, i);		
	}


	// Define something to hold the best hypotheses
	TopN<MyHypothesis> top;

	// our data
	//MyHypothesis::data_t mydata = {25, 36, 49, 25, 36, 49, 25, 36, 49, 25, 36, 49};	// squares in a range
	//MyHypothesis::data_t mydata = {3,4,6,8,12};	
	MyHypothesis::data_t mydata = { std::multiset<int>{2,4,32} };	

	// create a hypothesis
	auto h0 = MyHypothesis::make(&grammar);
	
	// and sample with just one chain
	MCMCChain samp(h0, &mydata, top);
	tic();
	samp.run(Control()); //30000);		
	tic();

	// print the results
	top.print();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;
}

#endif