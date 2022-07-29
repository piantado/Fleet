// A simple version of a generalized quantifier learner
// In this setup we get utterances like "All the squares are red" nd so the shape/color objects in 
// the utterance are the correct arguments to the quantifier, but it must sort out which is which 

// TODO: this current version does not have informativity weights -- coming soon!

// Add: weights, accuracy (presup and literal), 
// spreading -- bunny and classical
// only conservative in prior 

#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <exception>
#include <set>
#include <map>
#include <functional>


const std::vector<std::string> words = {"NA", "every", "some", "a", "one", "both", "neither", "the", "most", "no"}; 

static const double alpha_p = 0.95; 
static const double alpha_t = 0.8;

//const std::vector<size_t> data_amounts = {0, 1, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500};

//bool only_conservative = false; // do we only allow conservative quantifiers? (via the likelihood)
 
static const size_t MAX_NODES = 16;
 
const int NDATA = 1500; // how many data points to sample?

// Define some object types etc here
enum class MyColor { Red, Green, Blue,          __count };
enum class MyShape { Square, Circle, Triangle,  __count };
enum class TruthValue {False=0, True=1, Undefined=2};

#include "Object.h"

using MyObject = Object<MyShape, MyColor>;

using Set = std::multiset<MyObject>; // NOTE a MULT-set

struct Utterance {
	Set context;
	Set shape; // TODO: In the other implementation, these are sets matching in shape/color
	Set color;
	std::string word;
};

// our target stores a mapping from strings in w to functions that compute them correctly
std::map<std::string, std::function<TruthValue(Set,Set,Set)> > target;

#include "DSL.h"
#include "MyGrammar.h"
#include "MyHypothesis.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Fleet.h" 
#include "TopN.h"
#include "Random.h"
#include "ParallelTempering.h"

int main(int argc, char** argv){ 


	MyHypothesis::p_factor_propose=0.1;

	Fleet fleet("Adorable implementation of quantifier learner");
	fleet.initialize(argc, argv);
	
	
	{
		using namespace DSL;
		
		target["NA"] = +[](Set s, Set c, Set x) -> TruthValue { // you can always say this -- nothing
			return presup(true,true);
		};	
		
		target["every"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(not empty(s), subset(s,c));
		};	
		
		target["some"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(not empty(s), not empty(intersection(s,c)));
		};	
		
		target["one"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(true, singleton(intersection(s,c)));
		};		
				
		target["a"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(true, not empty(intersection(s,c)));
		};		
		
		target["both"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(doubleton(s), subset(s,c));
		};		
		
		target["neither"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(doubleton(s), empty(intersection(s,c)));
		};		
		
		
		target["the"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(singleton(s), subset(s,c));
		};	

		target["most"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(not empty(s), intersection(s,c).size() > s.size()/2 );
		};		
		
		target["no"] = +[](Set s, Set c, Set x) -> TruthValue {
			return presup(not empty(s), empty(intersection(s,c)));
		};	
		
	}
	
	
	//------------------
	// set up the data
	//------------------
	
	std::vector<Utterance> mydata;
	while(mydata.size() < NDATA) {
		
		Set context; 
		int ctx_size = 1+myrandom(10); // up to this many objects
		for(int c=0;c<ctx_size;c++) 
			context.insert( MyObject::sample() ); 
		
		// figure out what we're talking about here
		const MyColor c = static_cast<MyColor>(myrandom(static_cast<int>(MyColor::__count)));
		Set color = DSL::filter_color(c,context);
		
		const MyShape s = static_cast<MyShape>(myrandom(static_cast<int>(MyShape::__count)));
		const Set shape = DSL::filter_shape(s,context);
		
		// sample the word (don't need multisets here)
		std::set<std::string> wtrue;
		std::set<std::string> wpresup;
		
		// evaluate all the words
		for(auto& w : words) {
			auto o = target[w](shape, color, context);
			
			if(o == TruthValue::True)      
				wtrue.insert(w);
			
			if(o != TruthValue::Undefined) 
				wpresup.insert(w);
		}
		
		/// only use if there is something true to say
//		if(wtrue.size() == 0) continue; 
		
		// now sample
		std::string word; // 
		if(flip(alpha_p) and wpresup.size() > 0) {
			if(flip(alpha_t) and wtrue.size() > 0 ) 
				word = *sample<std::string, decltype(wtrue)>(wtrue).first;
			else              
				word = *sample<std::string, decltype(wpresup)>(wpresup).first;
		}
		else {
			word = *sample<std::string, decltype(words)>(words).first;
		}
		
		// if we print these out
		COUT word TAB static_cast<int>(target[word](shape, color, context)) TAB static_cast<int>(s) TAB static_cast<int>(c) << "\t";
		for(auto& o : context) COUT o;
		COUT "\n";
		
		
		Utterance u{.context=context, .shape=shape, .color=color, .word=word};		
		mydata.push_back(u);
	}
	

	
//	
//	Utterance u{ .context={{MyColor::Red, MyShape::Square}, {MyColor::Blue,MyShape::Rectangle}},
//				 .color=MyColor::Red,
//				 .shape=MyShape::Square,
//				 .word="every"
//				 };
//	mydata.push_back(u);
//		
	//////////////////////////////////
	// run MCMC
	//////////////////////////////////
	
	TopN<MyHypothesis> top;
	top.print_best = true;

	auto h0 = MyHypothesis::sample(words);
	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 10.0);
//	MetropolisHastings chain(h0, &mydata);

	for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
		UNUSED(h);
	}
	
	top.print();
}