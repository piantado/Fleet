
// Maybe add filter, higher-order predicates?

#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <exception>
#include <map>

#include "BindingTree.h"

// need a fixed order of words to correspond to factor levels
// We use REXP here (John, Mary, etc) so that we don't have to distinguish which
std::vector<std::string> words = {"REXP", "him", "his", "he", "himself"}; // his?

static const double alpha = 0.99999; 
const int NDATA = 10; // how many data points from each sentence are we looking at?

using S = std::string;

/**
 * @class TreeException
 * @author Steven Piantadosi
 * @date 08/09/21
 * @file Main.cpp
 * @brief TreeExceptions get thrown when we try to do something crazy in the tree
 */
class TreeException : public std::exception {};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"
#include "Singleton.h"

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*>,
				  public Singleton<MyGrammar> {
					  
	using Super =        Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*>;
	using Super::Super;
public:

	MyGrammar() {

		add("null(%s)",  +[](BindingTree* x) -> bool { 
			return x == nullptr;
		}); 
		
		add("parent(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x == nullptr) throw TreeException();
			return x->parent; 
		});
		
		add("root(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x==nullptr) throw TreeException();			
			return x->root();
		});		

		// linear order predicates
//		add("linear(%s)",        +[](BindingTree* x) -> int { 
//			if(x==nullptr) throw TreeException();			
//			return x->linear_order; 
//		});
//		
//		add("traversal(%s)",        +[](BindingTree* x) -> int { 
//			if(x==nullptr) throw TreeException();			
//			return x->traversal_order; 
//		});
//		
//		add("eq(%s,%s)",         +[](int a, int b) -> bool { return (a==b); });		
//		add("gt(%s,%s)",         +[](int a, int b) -> bool { return (a>b); });
		
		// string equality
		add("eq(%s,%s)",   +[](S a, S b) -> bool { return (a==b); });		
		add("eq(%s,%s)",   +[](POS a, POS b) -> bool { return (a==b); });		
		add("eq(%s,%s)",   +[](BindingTree* x, BindingTree* y) -> bool { return x == y;});

		
		// pos predicates
		add("pos(%s)",           +[](BindingTree* x) -> POS { 
			if(x==nullptr) throw TreeException();
			return x->pos;
		});		

		// tree predicates		
		add("coreferent(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x==nullptr) throw TreeException();
			return x->coreferent();
		});
		
		add("corefers(%s)",  +[](BindingTree* x) -> bool { 
			if(x==nullptr) throw TreeException();
			return x->coreferent() != nullptr;
		});
				
		add("leaf(%s)",  +[](BindingTree* x) -> bool { 
			if(x==nullptr) throw TreeException();
			return x->nchildren() == 0;
		});
								
		add("word(%s)",  +[](BindingTree* x) -> S { 
			if(x==nullptr) throw TreeException();
			if(x->target) throw TreeException(); // well it's very clever -- we can't allow label on the target or the problem is trivial
			return x->word;
		});
				
		add("dominates(%s,%s)",  +[](BindingTree* x, BindingTree* y) -> bool { 
			// NOTE: a node does dominate itself
			if(y == nullptr or x == nullptr) 
				throw TreeException();
			
			while(true) {
				if(y == x) 	   			        return true;
				else if(y->parent == nullptr)   return false;
				else							y = y->parent;
			}
		});
		
		add("inside(%s,%s)",  +[](BindingTree* x, POS y) -> bool { 
			if(x == nullptr) 
				throw TreeException();
			
			while(true) {
				if(x->pos == y) 		        return true;
				else if(x->parent == nullptr)   return false;
				else							x = x->parent;
			}
		});
		
		add("first-dominating(%s,%s)",  +[](POS s, BindingTree* x) -> BindingTree* { 	
			
			if(x == nullptr) 
				throw TreeException();
			
			while(x != nullptr) {
				if(x->pos == s)
					return x;
				
				x = x->parent;
			}
			
			return nullptr; // or throw TreeException()?
		});			
			
		add("true",          +[]() -> bool               { return true; });
		add("false",         +[]() -> bool               { return false; });
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
//		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
//		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,POS>);
//		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,BindingTree*>);
//	
		add("x",              Builtins::X<MyGrammar>, 5);	
		
		for(auto& w : words) {
			add_terminal<S>(Q(w), w, 5.0/words.size());
		}
		
		for(auto [l,p] : posmap) {
			add_terminal<POS>(Q(l), p, 5.0/posmap.size());		
		}
		
		
		// for absolute positions (up top 24)
//		for(int p=0;p<24;p++) {
//			add_terminal<int>(str(p), p, 1.0/24.);
//		}
//		
		// add recursive calls
		// 
		// 
		
		// NOTE THIS DOES NOT WORK WITH THE CACHED VERSION
//		for(size_t a=0;a<words.size();a++) {	
//			add("F"+str(a)+"(%s)" ,  Builtins::Recurse<MyGrammar>, 1.0, a);
//		}
	}
		
} grammar;


#include "LOTHypothesis.h"
#include "Timing.h"
#include "Lexicon.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	using output_t = Super::output_t;
	using data_t = Super::data_t;
	
	void const* cached_data = nullptr; // when null, we recompute the cache
	std::vector<output_t> cache; 
	
	
	
	InnerHypothesis(const InnerHypothesis& c) : InnerHypothesis() {
		Super::operator=(c); // copy all this garbage -- not sure what to do here
		cache = c.cache; cached_data = c.cached_data;
	}
	InnerHypothesis(const InnerHypothesis&& c) : InnerHypothesis() {
		Super::operator=(c); // copy all this garbage -- not sure what to do here
		cache = c.cache; cached_data = c.cached_data;
	}
	InnerHypothesis& operator=(const InnerHypothesis& c) {
		Super::operator=(c);
		cache = c.cache; cached_data = c.cached_data;
		return *this;
	}
	InnerHypothesis& operator=(const InnerHypothesis&& c) {
		Super::operator=(c);
		cache = c.cache; cached_data = c.cached_data;
		return *this;
	}

	
	
	virtual double compute_prior() override {
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * this_totheses */
		if(this->value.count() > 16) return this->prior = -infinity;
		else					     return this->prior = this->get_grammar()->log_probability(value);
	}
	
	// A cached version that will call on a whole dataset, but remember if we've done it before
	template<typename dt>
	void cachedCallOnData(const dt& data) {
		
		// NOTE THIS DOES NOT WORK IF WE HAVE RECURSION 
		
		if(cached_data != &data) {
			PRINTN("CACHE MISS", this, cached_data, string());
			cache.resize(data.size(), false);
			for(size_t di=0;di<data.size();di++) {
				try { 				
					cache[di] = this->callOne(data[di].input, false); 
				} catch( TreeException& e){  continue; }
			}
			// store who is cached
			cached_data = &data;
		}
		else {
			PRINTN("CACHE HIT", this, cached_data, string());
			
		}
	}
	
	
	void set_value(Node&  v) { 
		Super::set_value(v);
		cached_data = nullptr; // tell it to recompute
	}
	void set_value(Node&& v) { 
		Super::set_value(v);
		cached_data = nullptr; // tell it to recompute
	}
	
	
	[[nodiscard]] virtual std::pair<InnerHypothesis,double> propose() const override {
		
		std::pair<Node,double> x;

		if(flip(0.5))       x = Proposals::regenerate(&grammar, value);	
		else if(flip(0.1))  x = Proposals::sample_function_leaving_args(&grammar, value);
		else if(flip(0.1))  x = Proposals::swap_args(&grammar, value);
		else if(flip())     x = Proposals::insert_tree(&grammar, value);	
		else                x = Proposals::delete_tree(&grammar, value);			
		
		return std::make_pair(InnerHypothesis(std::move(x.first)), x.second); 
	}	

};


class MyHypothesis : public Lexicon<MyHypothesis, InnerHypothesis, BindingTree*, std::string> {
	// Takes a node (in a bigger tree) and a word
	using Super = Lexicon<MyHypothesis, InnerHypothesis, BindingTree*, std::string>;
	using Super::Super; // inherit the constructors
public:	

	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		
		// set to null so that we get an error on recurse
		for(auto& f : factors) f.program.loader = nullptr; 
		
		// make sure everyone's cache is right on this data
		for(auto& f : factors) f.cachedCallOnData(data); 
		PRINTN("------------");
		
		// The likelihood here samples from all words that are true
		likelihood = 0.0;
		for(size_t di=0;di<data.size();di++) {
			auto& d = data[di];
			
			// see how many factors are true:
			bool wtrue = false; // was w true?
			int  ntrue = 0; // how many are true?
			
			// see which words are permitted
			for(size_t wi=0;wi<words.size();wi++) {
				auto b = factors[wi].cache[di];
				
				ntrue += 1*b;
				
				if(d.output == words[wi]) wtrue = b;			
			}
			
			// COUT wtrue TAB ntrue TAB d.output TAB string() ENDL;
			likelihood += NDATA * log( (wtrue ? d.reliability/ntrue : 0.0) + 
							           (1.0-d.reliability)/words.size());
		}
		
		return likelihood; 
	 }

//	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
//		
//		// need to set this so that if/when we recurse, we use the lexicon
//		for(auto& f : factors) f.program.loader = this; 
//		
//		// The likelihood here samples from all words that are true
//		likelihood = 0.0;
//		for(const auto& di : data) {
//			assert(di.input != nullptr);
//			
//			// see how many factors are true:
//			bool wtrue = false; // was w true?
//			int  ntrue = 0; // how many are true?
//			
//			// see which words are permitted
//			for(size_t wi=0;wi<words.size();wi++) {
//				
//				bool b = false; // default value
//				try { 				
//					b = factors[wi].callOne(di.input, false); 
////				} catch( TreeException& e){ likelihood -= 1000; } // NOTE: Additional penalty here for exceptions
//				} catch( TreeException& e){  return likelihood = -infinity; } // Muuuch faster here because we short circuit out
//				
//				ntrue += 1*b;
//				
//				if(di.output == words[wi]) 	
//					wtrue = b;
//				
//			}
//			
//			// COUT wtrue TAB ntrue TAB di.output TAB string() ENDL;
//			likelihood += NDATA * log( (wtrue ? di.reliability/ntrue : 0.0) + 
//							           (1.0-di.reliability)/words.size());
//		}
//		
//		return likelihood; 
//	 }
//	 
	 
};




///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Fleet.h" 

#include "TopN.h"
#include "Fleet.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"
#include "SExpression.h"

int main(int argc, char** argv){ 
	
	Fleet fleet("Learn principles A,B,C of binding theory");
	fleet.initialize(argc, argv);
	
	//------------------
	// set up the data
	//------------------
	MyHypothesis h0;
	for(size_t w=0;w<words.size();w++){ // for each word
		h0.factors.push_back(InnerHypothesis::sample());
	}

	MyHypothesis::p_factor_propose = 0.1;

	// convert to data
	MyHypothesis::data_t mydata;
	std::vector<std::string> raw_data; 
	
	for(auto& [ds, sentence] : read_csv<2>("lasnik-extensive.csv", false, ',')){
		
		// look at tokenization
		// for(auto k : SExpression::tokenize(ds)) { COUT ">>" << k << "<<" ENDL;}
		
		BindingTree* t = new BindingTree(SExpression::parse<BindingTree>(ds));	
		assert(t != nullptr);
		
		// and set the linear order of t
		// and count how many have coreferents (we need to make that many copies)
		int lc=0; 
		int tc=0;
		int ncoref = 0;
		for(auto& n : *t) { 
			
			if(n.referent != -1 and n.nchildren()==0) // we only count simple terminal ones
				ncoref++;
			
			if(n.is_terminal()) n.linear_order = lc++;
			else                n.linear_order = -1;
			n.traversal_order = tc++;
		}
//		PRINTN(t->string());
		
		for(int cr=0;cr<ncoref;cr++) {
			raw_data.push_back(ds); // save for later in case we want to see
		
			BindingTree* myt = new BindingTree(*t);

			// find the i'th coreferent and label it as such
			int cntcoref = 0;
			for(auto& n : *myt) {
				if(n.referent != -1 and n.nchildren()==0) 
					cntcoref++;
					
				if(cntcoref > cr) {
					n.target = true;
					break;
				}
			}
			
			// assert that there is only one target per tree
			assert( myt->sum( +[](const BindingTree& bt) -> double { return 1.0*bt.target; }) == 1); 
			
			PRINTN("#", myt->string());
//			COUT "#" TAB str(myt->get_target()) TAB str(myt->get_target()->coreferent()) TAB "\n" ENDL;
			
			MyHypothesis::datum_t d1 = {myt->get_target(), myt->get_target()->word, alpha};	
			mydata.push_back(d1);
		}
		
		delete t;
	}
		
	//////////////////////////////////
	// run MCMC
	//////////////////////////////////
	
	TopN<MyHypothesis> top;

	top.print_best = true;
//	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 10.0);
	MCMCChain chain(h0, &mydata);
	chain.temperature = 0.10;
	
	for(auto& h : chain.run(Control()) | top | print(FleetArgs::print)) {
		UNUSED(h);
	}
	
	top.print();
	
	
	MyHypothesis best = top.best();
	for(auto& f : best.factors) f.program.loader = &best; 
	
	for(size_t i=0;i<mydata.size();i++) {
		auto& di = mydata[i];
		PRINTN("#", i, mydata.size(), di.input->root()->string()); //.at(i));
		
		size_t fi = 0;
		for(auto& w : words) {
	
			int b = -1; // default value
			try { 				
				b = 1*best.factors[fi].callOne(di.input, false); 
			} catch( TreeException& e) {  }
			PRINTN("#", b, w);
			++fi;
		}
	}
}