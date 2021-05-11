
#include <string>

using S = std::string; // just for convenience

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables (which may get overwritten)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

S alphabet = "01"; // the alphabet we use (possibly specified on command line)
thread_local S datastr  = "01,01001,010010001,01001000100001"; // the data, comma separated
const float strgamma = 0.01; //75; // penalty on string length
const size_t MAX_LENGTH = 64; // longest strings cons will handle

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,  S,bool>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("tail(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : s.substr(1,S::npos)); });
		add("head(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : S(1,s.at(0))); });
//
//		add("pair(%s,%s)",   +[](S a, S b) -> S { 
//			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
//			else                     				 return a+b; 
//		});

		add_vms<S,S,S>("pair(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			S b = vms->getpop<S>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
			else 									 a += b; 
		}));

		add("\u00D8",        +[]()         -> S          { return S(""); });
		add("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; });

		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("x",             Builtins::X<MyGrammar>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("flip()",        Builtins::Flip<MyGrammar>, 10.0);
		add("recurse(%s)",   Builtins::Recurse<MyGrammar>);
			
		for(const char c : alphabet) {
			add_terminal( Q(S(1,c)), S(1,c), 5.0/alphabet.length());
		}
		
	}
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
		
		// This would be a normal call:
		const auto out = call(x.input, ""); 

		// Or we can call and get back a list of completed virtual machine states
		// these store a bit more information, like runtime counts and haven't computer the
		// marginal probability of strings
//		auto v = call_vms(x.input, "<err>", this); // return the states instead of the marginal outputs
//
//		// compute a "runtime" prior penalty
//		// NOTE this is fine since a Bayesable first computes the prior before the likelihood so this will not be overwritten
//		for(auto& vi : v) {
//			prior += -exp(vi.lp)*vi.runtime_counter.total;
//		}
//		// and convert v to a disribution on strings
//		auto out = marginal_vms_output(v);

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

// if we define DO_NOT_INCLUDE_MAIN then we can import everything *except* the below
#ifndef DO_NOT_INCLUDE_MAIN

#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 
#include "Builtins.h"
#include "VMSRuntimeError.h"

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	//------------------
	// Set up the grammar
	//------------------	
	
	MyGrammar grammar;
	
	//------------------
	// top stores the top hypotheses we have found
	//------------------	
	
	TopN<MyHypothesis> top;
	
	//------------------
	// set these global variables to make VMS a little faster, less accurate
	//------------------
	
	VirtualMachineControl::MAX_STEPS = 256;
	VirtualMachineControl::MAX_OUTPUTS = 256;
	
	//------------------
	// set up the data
	//------------------
	
	// mydata stores the data for the inference model
	auto mydata = string_to<MyHypothesis::data_t>(datastr);
		
	/// check the alphabet
	assert(not contains(alphabet, ":"));// can't have these or else our string_to doesn't work
	assert(not contains(alphabet, ","));
	for(auto& di : mydata) {
		for(auto& c: di.output) {
			assert(contains(alphabet, c));
		}
	}
	
	//------------------
	// Run
	//------------------

			
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
	
//	top.print_best = true;
//	auto h0 = MyHypothesis::make(&grammar);
//	ParallelTempering samp(h0, &mydata, top, FleetArgs::nchains, 1000.0);
//	samp.run(Control(), 100, 30000);		
//	

//	top.print_best = true; // print out each best hypothesis you find
//	auto h0 = MyHypothesis::make(&grammar);
//	MCMCChain c(h0, &mydata, top);
//	//c.temperature = 1.0; // if you want to change the temperature -- note that lower temperatures tend to be much slower!
//	c.run(Control());

	// run multiple chains
	auto h0 = MyHypothesis::make(&grammar);
	ChainPool c(h0, &mydata, top, FleetArgs::nchains);
	//c.temperature = 1.0; // if you want to change the temperature -- note that lower temperatures tend to be much slower!
	c.run(Control());



	top.print();

	
}

#endif