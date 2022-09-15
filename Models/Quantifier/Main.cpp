// A simple version of a generalized quantifier learner
// In this setup we get utterances like "All the squares are red" nd so the shape/color objects in 
// the utterance are the correct arguments to the quantifier, but it must sort out which is which 

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

const std::vector<size_t> data_amounts = {0,100}; // {0, 1, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000};

//bool only_conservative = false; // do we only allow conservative quantifiers? (via the likelihood)
 
static const size_t MAX_NODES = 10;
 
//const int NDATA = 1000; // how many data points to sample?
const int PRDATA = 5000; // how much data for precision/recall?

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


std::vector<Utterance> prdata; // data for computing precision/recall

#include "DSL.h"
#include "MyGrammar.h"
#include "MyHypothesis.h"

// our target stores a mapping from strings in w to functions that compute them correctly
MyHypothesis target;
MyHypothesis bunny_spread;
MyHypothesis classical_spread;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Fleet.h" 
#include "TopN.h"
#include "Random.h"
#include "ParallelTempering.h"

MyHypothesis::datum_t sample_data() {
	
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
		Utterance utt{.context=context, .shape=shape, .color=color, .word=EMPTY_STRING};
		auto o = target.at(w).call(utt);
		
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
	
	return Utterance{.context=context, .shape=shape, .color=color, .word=word};	
}


int main(int argc, char** argv){ 


	MyHypothesis::p_factor_propose=0.1;

	Fleet fleet("Adorable implementation of quantifier learner");
	fleet.initialize(argc, argv);
	
	target["NA"]       = InnerHypothesis(grammar.simple_parse("presup(true,true)"));
	target["every"]    = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),subset(shape(x),color(x)))"));
	target["some"]     = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),not(empty(intersection(shape(x),color(x)))))"));
	target["one"]      = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),singleton(intersection(shape(x),color(x))))"));
	target["the"]      = InnerHypothesis(grammar.simple_parse("presup(singleton(shape(x)),singleton(intersection(shape(x),color(x))))"));
	target["a"]        = InnerHypothesis(grammar.simple_parse("presup(true,singleton(intersection(shape(x),color(x))))"));
	target["both"]     = InnerHypothesis(grammar.simple_parse("presup(doubleton(shape(x)),singleton(intersection(shape(x),color(x))))"));
	target["neither"]  = InnerHypothesis(grammar.simple_parse("presup(doubleton(shape(x)),empty(intersection(shape(x),color(x))))"));
	target["most"]     = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),card_gt(intersection(shape(x),color(x)),difference(shape(x),color(x))))"));
	target["no"]       = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),empty(intersection(shape(x),color(x))))"));
	
	bunny_spread = target;
	bunny_spread["every"]    = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),equal(shape(x),color(x)))"));
	
	classical_spread = target;
	classical_spread["every"]    = InnerHypothesis(grammar.simple_parse("presup(not(empty(shape(x))),and(equal(shape(x),color(x)), equal(shape(x), context(x))))"));
	
	// set up the data	
	for(size_t i=0;i<PRDATA;i++){
		prdata.push_back(sample_data());
	}
	
	
	/////////////////////////////////////////////
	// Now run: 
	/////////////////////////////////////////////
	
	TopN<MyHypothesis> top; // stored across rules
	
	for(size_t ndata : data_amounts) {
		
		std::vector<Utterance> mydata;
		for(size_t i=0;i<ndata;i++){			
			mydata.push_back(sample_data());
		}

		// update top on all new data
		TopN<MyHypothesis> newtop;
		for(auto h : top.values()) { // copy so we can modify
			
			h.clear_cache(); // previous data, clear out
			h.compute_posterior(mydata);
			newtop << h; 
		}
		top = newtop;
				
		target.clear_cache(); target.compute_posterior(mydata);
		bunny_spread.clear_cache(); bunny_spread.compute_posterior(mydata);
		classical_spread.clear_cache(); classical_spread.compute_posterior(mydata);
		
		// run MCMC
		auto h0 = MyHypothesis::sample(words);
		ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 1.10);
		for(auto& h : chain.run(Control()) | top | printer(FleetArgs::print)) {
			UNUSED(h);
		}
				
		// now print		
		for(auto h : top.values()) {
			h.show(str(ndata));
		}
		
		
	}
}