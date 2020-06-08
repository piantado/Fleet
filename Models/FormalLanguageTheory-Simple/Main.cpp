
#include <string>

//#define DEBUG_MCMC

using S = std::string; // just for convenience

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables (which may get overwritten)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

S alphabet = "01"; // the alphabet we use (possibly specified on command line)
thread_local S datastr  = "01,01001,010010001,01001000100001"; // the data, comma separated
const float strgamma = 0.01; //75; // penalty on string length
const size_t MAX_LENGTH = 64; // longest strings cons will handle

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : s.substr(1,S::npos)); }),
	Primitive("head(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : S(1,s.at(0))); }),
	// We could call like this, but it's a little inefficient since it pops a string from the stack
	// and then pushes a result on.. much better to modify it
//	Primitive("pair(%s,%s)",   +[](S a, S b) -> S          { if(a.length() + b.length() > MAX_LENGTH) 
//																throw VMSRuntimeError;
//															else return a+b; 
//															}),
	// This version takes a reference for the first argument and that is assumed (by Fleet) to be the
	// return value. It is never popped off the stack and should just be modified. 
	Primitive("pair(%s,%s)",   +[](S& a, S b) -> void        { 
			if(a.length() + b.length() > MAX_LENGTH) 
				throw VMSRuntimeError;
			a.append(b); // modify on stack
	}), 
	
	Primitive("\u00D8",        +[]()         -> S          { return S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	
	
	// And add built-ins - NOTE these must come last
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::Flip("flip()", 10.0),
	Builtin::SafeRecurse<S,S>("F(%s)")	
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<S,bool> {
	using Super = Grammar<S,bool>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const datum_t& x) override {	
		const auto out = call(x.input, "<err>", this, 256, 256); //256, 256);
		
		const auto log_A = log(alphabet.size());
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		double lp = -infinity;
		for(auto& o : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, o.second + p_delete_append<strgamma,strgamma>(o.first, x.output, log_A));
		}
		return lp;
	}
		
//	[[nodiscard]] virtual std::pair<MyHypothesis,double> propose() const override {
//		
//		std::pair<Node,double> x;
//		if(flip()) {
//			x = Proposals::regenerate(grammar, value);	
//		}
//		else {
//			if(flip()) x = Proposals::insert_tree(grammar, value);	
//			else       x = Proposals::delete_tree(grammar, value);	
//		}
//		return std::make_pair(MyHypothesis(this->grammar, std::move(x.first)), x.second); 
//	}	
//
//	[[nodiscard]] virtual std::pair<MyHypothesis,double> propose() const {
//		auto g = grammar->generate<S>();
//		return std::make_pair(MyHypothesis(grammar, g), grammar->log_probability(g) - grammar->log_probability(value));
//	}	
	
	void print(std::string prefix="") override {
		// we're going to make this print by showing the language we created on the line before
		prefix = prefix+"#\n#" +  this->call("", "<err>").string() + "\n";
//		prefix += str(grammar->compute_enumeration_order(value)) + "\t"; 
		Super::print(prefix); 
	}
};


////////////////////////////////////////////////////////////////////////////////////////////
// This needs to be included last because it includes VirtualMachine/applyPrimitives.h
// which really requires Primitives to be defined already

#include "Top.h"
#include "ParallelTempering.h"
#include "FullMCTS.h"
#include "Astar.h"

#include "Fleet.h" 


int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	MyGrammar grammar(PRIMITIVES);

	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top(ntop);
	
	// here we create an alphabet op with an "arg" that stores the character (this is faster than alphabet.substring with i.arg as an index) 
	// here, op_ALPHABET converts arg to a string (and pushes it)
	for(size_t i=0;i<alphabet.length();i++) {
		grammar.add<S>     (BuiltinOp::op_ALPHABET, Q(alphabet.substr(i,1)), 5.0/alphabet.length(), (int)alphabet.at(i)); 
	}
	
	//------------------
	// set up the data
	//------------------
	
	// we will parse the data from a comma-separated list of "data" on the command line
	for(auto di : split(datastr, ',')) {
		// add check that data is in the alphabet
		for(auto& c : di) {
			assert(alphabet.find(c) != std::string::npos && "*** alphabet does not include all data characters");
		}
		
		// and add to data:
		mydata.push_back( MyHypothesis::datum_t({S(""), di}) );		
	}
	
	//------------------
	// Run
	//------------------

			
//	auto v = grammar.expand_from_names("F:NULL");
//	
//	auto nt = v.nt();
//	std::function isnt = [nt](const Node& n){ return (int)(n.nt() == nt and not n.is_null() );};
//	
//	CERR v.string() ENDL;
//	CERR v.get_nth(0, isnt)->string() ENDL;

//	IntegerizedStack is;
//	is.push(12);
//	is.push(33);
//	is.push(400);
//	CERR is.get_value() ENDL;
//	CERR is.pop() ENDL;
//	CERR is.pop() ENDL;
//	CERR is.pop() ENDL;
//	CERR is.pop() ENDL;
//	
	
//	Fleet::Statistics::TopN<MyHypothesis> tn(10);
//	for(enumerationidx_t z=0;z<10000000 and !CTRL_C;z++) {
//		auto n = grammar.expand_from_integer(0, z);
////		auto n = grammar.lempel_ziv_full_expand(0, z);
//		
//		MyHypothesis h(&grammar);
//		h.set_value(n);
//		h.compute_posterior(mydata);
//		
//		tn << h;
//		auto o  = grammar.compute_enumeration_order(n);
//		COUT z TAB o TAB h.posterior TAB h ENDL;
//	}
//	tn.print();
//
//	return 0;
	
//	MyHypothesis h0(&grammar);
//	h0 = h0.restart();
//	tic();
////	
//	ParallelTempering samp(h0, &mydata, top, nchains, 1000.0);
//	samp.run(Control(mcmc_steps, runtime, nthreads), 100, 300); //30000);		
//	
//	MCMCChain c(h0, &mydata, top);
	//c.temperature = 1.0; // if you want to change the temperature -- note that lower temperatures tend to be much slower!
//	c.run(Control(mcmc_steps, runtime, nthreads));


//	MyHypothesis h0(&grammar);
//	FullMCTSNode<MyHypothesis,TopN<MyHypothesis>> m(h0, explore, &mydata, top);
//	tic();
//	m.parallel_search(Control(mcts_steps, runtime, nthreads), h0);
//	tic();
//	m.print(h0, "tree.txt");
//	CERR "# MCTS size: " TAB m.size() ENDL;

	// Do some A* search -- here we maintain a priority queue of partially open nodes, sorted by 
	// their prior and a single sample of their likelihood (which we end up downweighting a lot) (see Astar.h)
	// This heuristic is inadmissable, but ends up working pretty well. 
	top.print_best = true;
	MyHypothesis h0(&grammar);
	Astar astar(h0,&mydata, top, 100.0);
	astar.run(Control(mcts_steps, runtime, nthreads));
	
	top.print();

	
}