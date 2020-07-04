// TODO: Include lienear order operations, so we can "rule those out"!


#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <set>
#include <map>

// need a fixed order of words to correspond to factor levels
std::vector<std::string> words = {"NONE", "John", "him", "he", "himself"};
static const double alpha = 0.99; 
 
#include "BindingTree.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"

typedef std::set<BindingTree*> TreeSet; // actually a set of pointers

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<bool,BindingTree*,TreeSet> {
	using Super = Grammar<bool,BindingTree*,TreeSet>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
/// This must be defined before we import Fleet because Fleet does some template
/// magic internally
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {

	Primitive("null(%s)",  +[](BindingTree* x) -> bool { 
		return x == nullptr;
	}, 1.0), 

	Primitive("(%s==%s)",  +[](BindingTree* x, BindingTree* y) -> bool { 
		return x == y;
	}, 1.0), 


	Primitive("parent(%s)",  +[](BindingTree* x) -> BindingTree* { 
		return x==nullptr ? nullptr : x->parent;
	}, 1.0), 

	Primitive("root(%s)",  +[](BindingTree* x) -> BindingTree* { 
		return x==nullptr ? nullptr : x->root();
	}, 1.0), 
	
	Primitive("isNP(%s)",  +[](BindingTree* x) -> bool { 
		if(x == nullptr) return false;
		return x->label == "NP";
	}, 1.0), 
	Primitive("isVP(%s)",  +[](BindingTree* x) -> bool { 
		if(x == nullptr) return false;
		return x->label == "VP";
	}, 1.0), 
//	Primitive("refers(%s)",  +[](BindingTree* x) -> bool { 
//		if(x == nullptr) return false;
//		return x->referent != -1;
//	}, 1.0), 
	
	Primitive("corefers(%s,%s)",  +[](BindingTree* x, BindingTree* y) -> bool { 
		if(x == nullptr or y == nullptr) return false;
		// we corefer only if we refer and we refer to the same thing
		return x->referent != -1 and x->referent == y->referent;
	}, 1.0), 
	
	Primitive("coreferent(%s)",  +[](BindingTree* x) -> BindingTree* {
		// returns the coreferent of this in the tree
		if(x == nullptr) return nullptr;
		std::function f = [&](BindingTree& t) -> bool {
			return (t.referent != -1) and 
				   (t.referent == x->referent) and 
				   (&t != x);
		};
		
		// if there is no co-referent, return x itself
		return x->root()->get_via(f);
		
	}, 1.0), 
	
	Primitive("dominates(%s,%s)",  +[](BindingTree* x, BindingTree* y) -> bool { 
		if(y == nullptr or x == nullptr) return false;
		
		while(true) {
			if(y->parent == x) 				return true;
			else if(y->parent == nullptr)   return false;
			else							y = y->parent;
		}
		
	}, 1.0), 
	
	Primitive("true",     +[]() -> bool { return true; }),
	Primitive("false",    +[]() -> bool { return false; }),

	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }),
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),

	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<BindingTree*>("x", 10.0)
};

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"
#include "Timing.h"
#include "Lexicon.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar>;
	using Super::Super; // inherit the constructors
};

class MyHypothesis : public Lexicon<MyHypothesis, InnerHypothesis, BindingTree*, std::string> {
	// Takes a node (in a bigger tree) and a word
	using Super = Lexicon<MyHypothesis, InnerHypothesis, BindingTree*, std::string>;
	using Super::Super; // inherit the constructors
	
public:	

	virtual double compute_prior() override {
		// this uses a proper prior which flips a coin to determine the number of factors
		return Super::compute_prior();
	}

	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// Check all the words that are true and select 
		likelihood = 0.0;
		for(const auto& di : data) {
			assert(di.input != nullptr);
			
			// see how many factors are true:
			bool wtrue = false;
			int  ntrue = 0;
			size_t fi = 0;
			for(auto& w : words) {
				bool b = factors[fi].callOne(di.input, false); 
				ntrue += b;
				if(di.output == w) {
					wtrue = b;
				}
				++fi;
			}
//			CERR log( (ntrue>0 and wtrue ? di.reliability/ntrue : 0.0) + 
//							   (1.0-di.reliability)/words.size())
//				TAB di TAB wtrue TAB ntrue TAB this->string() ENDL;
				
			likelihood += log( (wtrue ? di.reliability/ntrue : 0.0) + 
							   (1.0-di.reliability)/words.size());
		}
		
		likelihood *= 10;
		
		return likelihood; 
	 }
};

BindingTree* coref(BindingTree* x) {
	// returns the coreferent of this in the tree
	if(x == nullptr) return nullptr;
	
	std::function f = [&](BindingTree& t) -> bool {
		return (t.referent != -1) and 
			   (t.referent == x->referent) and 
			   (&t != x);
	};
		
	return x->root()->get_via(f);
}

std::string TreeStr(BindingTree* t){
	if(t == nullptr) return "nullptr";
	else return t->string();
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "Top.h"
#include "Fleet.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"
#include "SExpression.h"

int main(int argc, char** argv){ 
	
	Fleet fleet("Learn principles of binding theory");
	fleet.initialize(argc, argv);
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)	
	MyGrammar grammar(PRIMITIVES);
	
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top(ntop);
	
	//------------------
	// set up the data
	//------------------
	MyHypothesis h;
	for(size_t w=0;w<words.size();w++){ // for each word
		h.factors.push_back(InnerHypothesis(&grammar));
	}
	h = h.restart();

	std::vector<std::string> data_strings = {
		"(S (NP he.1) (VP (V saw)) (NP *himself.1))",
		"(S (NP *he.1) (VP (V saw)) (NP himself.1))",
		"(S (NP he.1) (VP (V saw)) (NP *him.2))",
		"(S (NP *he.1) (VP (V saw)) (NP him.2))",
		
		"(S (NP he.1) (VP (V saw)) (NP *John.2))",
		"(S (NP *he.1) (VP (V saw)) (NP John.2))",
		
		"(S (NP John.1) (VP (V saw)) (NP *himself.1))",
		"(S (NP John.1) (VP (V saw)) (NP *him.2))",
		
		"(S (NP *John.1) (VP (V saw)) (NP him.2))",
		"(S (NP *John.1) (VP (V saw)) (NP himself.1))",
		
	    "(S (NP Frank.1) (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP *himself.2)))))", 
        "(S (NP Frank.1) (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP *him.1)))))", 
        "(S (NP John.1)  (VP (V believed) (CC that) (S (NP *he.1) (VP (V tickled) (NP himself.1)))))", 
		"(S (NP John.1)  (VP (V believed) (CC that) (S (NP *he.1) (VP (V tickled) (NP Bill)))))", 
		"(S (NP Bill.1)  (VP (V believed) (CC that) (S (NP *he.1) (VP (V tickled) (NP *himself.1)))))", 
		"(S (NP Bill.1)  (VP (V believed) (CC that) (S (NP *he.1) (VP (V tickled) (NP *John.2)))))", 
		
		
        "(S (NP *John.1) (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP himself.2)))))", 
        "(S (NP *John.1) (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP him.1)))))", 
        "(S (NP *he.1)   (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP him.1)))))", 
        "(S (NP *he.1)   (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP John.1)))))", 
        "(S (NP *he.1)   (VP (V believed) (CC that) (S (NP Joe.2) (VP (V tickled) (NP himself.1)))))", 
        "(S (NP he.1)    (VP (V believed) (CC that) (S (NP John.2) (VP (V tickled) (NP *John.2)))))", 
		
	};
	
	// convert to data
	for(auto& ds : data_strings)  {
		BindingTree* t = new BindingTree(SExpression::parse<BindingTree>(ds));	
		t->print();
		
		//COUT TreeStr(t->get_starred()) TAB TreeStr(coref(t->get_starred())) TAB "\n" ENDL;
		
		MyHypothesis::datum_t d1 = {t->get_starred(), t->get_starred()->label, alpha};	
		mydata.push_back(d1);
	}
		
	
	// run a simple MCMC chain
	top.print_best = true;
	MCMCChain chain(h, &mydata, top);
	chain.run(Control(mcmc_steps,runtime));
	
	top.print();
}