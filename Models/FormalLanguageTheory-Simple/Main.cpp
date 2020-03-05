
#include <string>

using S = std::string; // just for convenience

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables (which may get overwritten)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

S alphabet = "01"; // the alphabet we use (possibly specified on command line)
//S datastr = "1011001110";
//S datastr  = "011,011011,011011011"; // the data, comma separated
//S datastr = "01,01001";
S datastr  = "01,01001,010010001,01001000100001"; // the data, comma separated
const double strgamma = 0.95; //75; // penalty on string length
const size_t MAX_LENGTH = 64; // longest strings cons will handle
	
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define FLEET_GRAMMAR_TYPES S,bool

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
			a = a+b; // modify on stack
	}), 
	
	Primitive("\u00D8",        +[]()         -> S          { return S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	
	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	
	
	// And add built-ins:
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::Flip("flip()", 10.0),
	Builtin::SafeRecurse<S,S>("F(%s)")	
};

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,S,S> {
public:
	using Super =  LOTHypothesis<MyHypothesis,Node,S,S>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const t_datum& x) override {	
		auto out = call(x.input, "<err>", this, 256, 256); //256, 256);
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		double lp = -infinity;
		for(auto& o : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, o.second + p_delete_append(o.first, x.output, 1.-strgamma, 1.-strgamma, alphabet.size()));
		}
		return lp;
	}
		
	[[nodiscard]] virtual std::pair<MyHypothesis,double> propose() const override {
		
		std::pair<Node,double> x;
		if(flip()) {
			x = Proposals::regenerate(grammar, value);	
		}
		else {
			if(flip()) x = Proposals::insert_tree(grammar, value);	
			else       x = Proposals::delete_tree(grammar, value);	
		}
		return std::make_pair(MyHypothesis(this->grammar, std::move(x.first)), x.second); 
	}	
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

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("A simple, one-factor formal language learner");
	app.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	app.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	// mydata stores the data for the inference model
	MyHypothesis::t_data mydata;
	
	// top stores the top hypotheses we have found
	Fleet::Statistics::TopN<MyHypothesis> top(ntop);
	
	// declare a grammar with our primitives
	Grammar grammar(PRIMITIVES);
	
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
		mydata.push_back( MyHypothesis::t_datum({S(""), di}) );
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
////		auto n = grammar.expand_from_integer(0, z);
//		auto n = grammar.lempel_ziv_full_expand(0, z);
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
	
	MyHypothesis h0(&grammar);
	h0 = h0.restart();

//	MyHypothesis h(&grammar);
//	h = h.restart();
//	h.compute_posterior(mydata);
//	for(size_t i=0;i<mcmc_steps and !CTRL_C;i++) {
//		auto [p,fb] = h.propose();
//		p.compute_posterior(mydata);
//		if( uniform() < exp(p.posterior - h.posterior - fb) ) {
//			h = p;
//		}
//		top << h;
//	}

//	for(size_t i =0;i<100;i++) {
//		MyHypothesis h = h0.restart();
//		CERR grammar.log_probability(h.value) TAB h ENDL;
//	}
//	
//	return 0;
	
//	COUT h0 ENDL;
//	for(auto& x : h0.value) {
//		COUT x.can_resample TAB x ENDL;
//	}
//	
//	for(size_t j=0;j<100;j++) {
//		COUT sample<Node,Node>(h0.value, Proposals::can_resample).first->string() ENDL;
//	}
//	
////	return 0;
	
	ParallelTempering samp(h0, &mydata, top, nchains, 1000.0);
	tic();
	samp.run(Control(mcmc_steps, runtime, nthreads), 1000, 3000); //30000);		
	tic();
	
//	 Show the best we've found
	top.print();

	
//	MCMCChain c(h0, &mydata, top);
//	tic();
//	c.run(Control(mcmc_steps, runtime, nthreads));
//	tic();
//	
//	for(auto& h : top.values()) {
//		COUT top[h] TAB h.posterior TAB h.prior TAB h.likelihood TAB h.string() ENDL;		
//	}
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;
}