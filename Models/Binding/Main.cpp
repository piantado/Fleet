
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
std::vector<std::string> words = {"REXP", "him", "his", "he", "himself"};
std::vector<double> data_amounts = {1, 2, 5, 10, 15, 20, 30, 40, 50, 75, 100, 110, 125, 150, 175, 200, 300, 400, 500, 1000, 1500, 2000, 2500, 5000};

static const double alpha = 0.95; 
int NDATA = 10; // how many data points from each sentence are we looking at?

const double TERMINAL_P = 3.0;

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

		// equality
		add("eq_bool(%s,%s)",   +[](bool a, bool b) -> bool { return (a==b); });		
		add("eq_str(%s,%s)",   +[](S a, S b) -> bool { return (a==b); });		
		add("eq_pos(%s,%s)",   +[](POS a, POS b) -> bool { return (a==b); });		
		add("eq_bt(%s,%s)",   +[](BindingTree* x, BindingTree* y) -> bool { return x == y;});

		// pos predicates -- part of speech (NOT position)
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
			// NOTE: a node will dominate itself
			if(y == nullptr or x == nullptr) 
				throw TreeException();
							
			while(true) {
				if(y == nullptr) return false; // by definition, null doesn't dominate anything
				else if(y == x) return true;
				y = y->parent;
			}
		});
		
		add("first-dominating(%s,%s)",  +[](POS s, BindingTree* x) -> BindingTree* { 	
			if(x == nullptr) throw TreeException();
			
			while(true) {
				if(x == nullptr) return nullptr; // by definition, null doesn't dominate anything
				else if(x->pos == s) return x;
				x = x->parent;
			}
		});			
			
		add("true",          +[]() -> bool               { return true; }, TERMINAL_P);
		add("false",         +[]() -> bool               { return false; }, TERMINAL_P);
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,POS>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,BindingTree*>);

		add("x",              Builtins::X<MyGrammar>, 10);	
		
		for(auto& w : words) {
			add_terminal<S>(Q(w), w, TERMINAL_P/words.size());
		}
		
		for(auto [l,p] : posmap) {
			add_terminal<POS>(Q(l), p, TERMINAL_P/posmap.size());		
		}
		
		// NOTE THIS DOES NOT WORK WITH THE CACHED VERSION
//		add("F(%s,%s)" ,  Builtins::LexiconRecurse<MyGrammar,S>); // note the number of arguments here
	}
		
} grammar;


#include "LOTHypothesis.h"
#include "Timing.h"
#include "Lexicon.h"
#include "CachedCallHypothesis.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar>, 
						public CachedCallHypothesis<InnerHypothesis,bool> {
public:
	using Super = LOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	using CCH = CachedCallHypothesis<InnerHypothesis,bool>;
	
	using output_t = Super::output_t;
	using data_t = Super::data_t;
	
	InnerHypothesis(const InnerHypothesis& c) : Super(c), CCH(c) {}	
	InnerHypothesis(const InnerHypothesis&& c) :  Super(c), CCH(c) { }	
	
	InnerHypothesis& operator=(const InnerHypothesis& c) {
		Super::operator=(c);
		CachedCallHypothesis::operator=(c);
		return *this;
	}
	InnerHypothesis& operator=(const InnerHypothesis&& c) {
		Super::operator=(c);
		CachedCallHypothesis::operator=(c);
		return *this;
	}
	
	void set_value(Node&  v) { 
		Super::set_value(v);
		CachedCallHypothesis::clear_cache();
	}
	void set_value(Node&& v) { 
		Super::set_value(v);
		CachedCallHypothesis::clear_cache();
	}
	
	virtual double compute_prior() override {
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * this_totheses */
		if(this->value.count() > 16) return this->prior = -infinity;
		else					     return this->prior = this->get_grammar()->log_probability(value);
	}
	
	[[nodiscard]] virtual std::optional<std::pair<InnerHypothesis,double>> propose() const override {
		
		std::optional<std::pair<Node,double>> p;

		if(flip(0.5))       p = Proposals::regenerate(&grammar, value);	
		else if(flip(0.1))  p = Proposals::sample_function_leaving_args(&grammar, value);
		else if(flip(0.1))  p = Proposals::swap_args(&grammar, value);
		else if(flip())     p = Proposals::insert_tree(&grammar, value);	
		else                p = Proposals::delete_tree(&grammar, value);			
		
		if(not p) return {};
		auto x = p.value();
		
		return std::make_pair(InnerHypothesis(std::move(x.first)), x.second); 
	}	

};


class MyHypothesis : public Lexicon<MyHypothesis, std::string, InnerHypothesis, BindingTree*, std::string> {
	// Takes a node (in a bigger tree) and a word
	using Super = Lexicon<MyHypothesis, std::string, InnerHypothesis, BindingTree*, std::string>;
	using Super::Super; // inherit the constructors
public:	

	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		
		// TODO: Can set to null so that we get an error on recurse
		for(auto& [k, f] : factors) f.program.loader = this; 
		
		// make sure everyone's cache is right on this data
		for(auto& [k, f] : factors) {
			//f.clear_cache(); // if we always want to recompute (e.g. if using recursion)
			f.cached_callOne(data); 
			
			// now if anything threw an error, break out, we don't have to compute
			if(f.got_error) 
				return likelihood = -infinity;
		}
		
		// The likelihood here samples from all words that are true
		likelihood = 0.0;
		for(size_t di=0;di<data.size();di++) {
			auto& d = data[di];
			
			// see how many factors are true:
			bool wtrue = false; // was w true?
			int  ntrue = 0; // how many are true?
			
			// see which words are permitted
			for(const auto& w : words) {
				auto b = factors[w].cache.at(di);
				
				ntrue += 1*b;
				
				if(d.output == w) wtrue = b;			
			}

			// Noisy size-principle likelihood
			likelihood += (double(NDATA)/double(data.size())) * log( (wtrue ? d.reliability/ntrue : 0.0) + 
							                                         (1.0-d.reliability)/words.size());
									   
			if(likelihood < breakout) return likelihood = -infinity;

		}
		
		return likelihood; 
	 }	 	

	virtual void print(std::string prefix="") override {
		
		extern MyHypothesis::data_t target_precisionrecall_data;
		extern MyHypothesis target;

		std::lock_guard guard(output_lock);
		
		COUT std::setprecision(5) << prefix << NDATA TAB this->posterior TAB this->prior TAB this->likelihood TAB "";
		
		// when we print, we are going to compute overlap with each target item
		for(auto& w : words) {
			int nagree = 0;
			int ntot = 0;
			
			for(size_t di=0;di<target_precisionrecall_data.size();di++) {
//				auto& d = target_precisionrecall_data[di];
			
				if(factors[w].cache.at(di) == target.factors[w].cache.at(di)) {
					++nagree;
				}
				++ntot;
			}
			
			COUT float(nagree)/float(ntot) TAB "";
		}
		
		COUT QQ(this->string()) ENDL;

	}
 
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

MyHypothesis::data_t target_precisionrecall_data; // data for computing precision/recall 
MyHypothesis target;
	
int main(int argc, char** argv){ 
	
	int include_linear = 1; 
	int include_absolute = 1; 
	
	Fleet fleet("Learn principles of binding theory");
	fleet.add_option("--ndata",   NDATA, "Run at a different likelihood temperature (strength of data)"); 
	fleet.add_option("--include-linear",  include_linear, "Should we include primitives for linear order?"); 
	fleet.add_option("--include-absolute",  include_absolute, "Should we include absolute primitives?"); 
	fleet.initialize(argc, argv);
	
	//------------------
	// Add linear hypotheses if needed
	//------------------
	
	if(include_absolute) {
		
		// for absolute positions (up top 24)
		for(int p=0;p<24;p++) {
			grammar.add_terminal<int>("abs"+str(p), p, TERMINAL_P/24.);
		}	
	}
	
	if(include_linear) {
		// linear order predicates
		grammar.add("linear(%s)",        +[](BindingTree* x) -> int { 
			if(x==nullptr) throw TreeException();			
			return x->linear_order; 
		});
		
		grammar.add("gt(%s,%s)",         +[](int a, int b) -> bool { return (a>b); });
		
	}

	// these primitives get included for either (but only once)
	if(include_absolute or include_linear) {
		
		grammar.add("eq_int(%s,%s)",   +[](int a, int b) -> bool { return (a==b); });		
		
		grammar.add("if(%s,%s,%s)",  Builtins::If<MyGrammar,int>);
	}
	


	//------------------
	// set up the data
	//------------------
	
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
		int ncoref = 0;
		for(auto& n : *t) { 
			
			if(n.referent != -1 and n.nchildren()==0) // we only count simple terminal ones
				ncoref++;
			
			if(n.is_terminal()) n.linear_order = lc++;
			else                n.linear_order = -1;
		}
		// PRINTN(t->string());
		
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
			
			//PRINTN("#", myt->string());
			
			MyHypothesis::datum_t d1 = {myt->get_target(), myt->get_target()->word, alpha};	
			mydata.push_back(d1);
			
			target_precisionrecall_data.push_back(d1);
		}
		
		delete t;
	}
		
	
	//------------------
	// Make target hypothesis to compare
	//------------------
	
	//"REXP", "him", "his", "he", "himself"
	target["REXP"] = InnerHypothesis(grammar.simple_parse("not(and(corefers(x),dominates(parent(coreferent(x)),x)))"));
	target["him"] = InnerHypothesis(grammar.simple_parse("eq_bool(eq_pos('NP-O',pos(x)),null(first-dominating('PP',x)))"));
	target["his"] = InnerHypothesis(grammar.simple_parse("eq_pos('NP-POSS',pos(parent(x)))"));
	target["he"] = InnerHypothesis(grammar.simple_parse("eq_pos('S',pos(parent(x)))"));
	target["himself"] = InnerHypothesis(grammar.simple_parse("and(corefers(x),dominates(parent(coreferent(x)),x))"));

//	MyHypothesis target2;
//	target2["REXP"] = InnerHypothesis(grammar.simple_parse("not(and(corefers(x),dominates(parent(coreferent(x)),x)))"));
//	target2["him"] = InnerHypothesis(grammar.simple_parse("eq_bool(eq_pos('NP-O',pos(x)),null(first-dominating('PP',x)))"));
//	target2["his"] = InnerHypothesis(grammar.simple_parse("eq_pos('NP-POSS',pos(parent(x)))"));
//	target2["he"] = InnerHypothesis(grammar.simple_parse("eq_pos('S',pos(parent(x)))"));
//	target2["himself"] = InnerHypothesis(grammar.simple_parse("and(eq_bool(eq_pos('NP-POSS',pos(parent(x))),eq_pos('NP-S',pos(x))),corefers(x))"));
//	
//	// Find sentences where these are different
//	for(auto& di : mydata){ 
//	
//		// make a little mini dataset
//		MyHypothesis::data_t thisdata;
//		thisdata.push_back(di);
//		
//		for(auto& w:words) {
//			target[w].clear_cache();
//			target2[w].clear_cache();
//		}
//		
//		target.compute_posterior(thisdata);
//		target2.compute_posterior(thisdata);
//		
//		if(target.likelihood != target2.likelihood)  {
//			PRINTN(target.likelihood, target2.likelihood, di.input->root()->string());
//		}
//	
//	}
//	
//	return 0;
//	
	
	
	PRINTN("# Target:", target.compute_posterior(mydata));


	//////////////////////////////////
	// run on increasing amounts of data
	//////////////////////////////////

	TopN<MyHypothesis> top;

	// set up the hypothesis
	MyHypothesis h0;
	for(const auto& w : words) {
		h0[w] = InnerHypothesis::sample();
	}
	MyHypothesis::p_factor_propose = 0.2;
	
	
	for(auto di : data_amounts) {
		
		// update data and top
		NDATA = di; // used in compute_posterior
		top = top.compute_posterior(mydata);
		
		ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 1.20);
		for(auto& h : chain.run(Control()) | top | print(FleetArgs::print)) {
			UNUSED(h);
		}

		// start from the best next time
		h0 = top.best();
		
		top.print();		
	}
	
	//////////////////////////////////
	// run MCMC
	//////////////////////////////////
	
//	TopN<MyHypothesis> top;
////	top.print_best = true;
//
//	ParallelTempering chain(h0, &mydata, FleetArgs::nchains, 1.20);
////	MCMCChain chain(h0, &mydata);
//	for(auto& h : chain.run(Control()) | top | print(FleetArgs::print)) {
//		UNUSED(h);
//	}
//	
//	top.print();

	//------------------
	// Print all the data
	//------------------
		
//	for(auto& f : best.factors) f.program.loader = &best; 
//	
//	for(size_t i=0;i<mydata.size();i++) {
//		auto& di = mydata[i];
//		PRINTN("#", i, mydata.size(), di.input->root()->string()); //.at(i));
//		
//		size_t fi = 0;
//		for(auto& w : words) {
//	
//			int b = -1; // default value
//			try { 				
//				b = 1*best.factors[fi].callOne(di.input, false); 
//			} catch( TreeException& e) {  }
//			PRINTN("#", b, w);
//			++fi;
//		}
//	}
}