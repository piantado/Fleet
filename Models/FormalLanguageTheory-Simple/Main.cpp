
#include <string>
#include <unistd.h> // for getpid
using S = std::string; // just for convenience

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Set up some basic variables (which may get overwritten)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

S alphabet = "01"; // the alphabet we use (possibly specified on command line)
thread_local S datastr  = "01,01001,010010001,01001000100001"; // the data, comma separated -- this data should be able to get a posterior of -34.9
const float strgamma = 0.01; //75; // penalty on string length
const size_t MAX_LENGTH = 64; // longest strings cons will handle

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,  S,char,bool>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("tail(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : s.substr(1,S::npos)); });
		add("head(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : S(1,s.at(0))); });
//
//		add("append(%s,%s)",   +[](S a, S b) -> S { 
//			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
//			else                     				 return a+b; 
//		});

		add_vms<S,S,S>("append(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			S b = vms->getpop<S>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
			else 									 a += b; 
		}));
		
		add_vms<S,S,char>("pair(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			char b = vms->getpop<char>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + 1 > MAX_LENGTH) throw VMSRuntimeError();
			else 						    a += b; 
		}));

		add("\u00D8",        +[]()         -> S          { return S(""); }, 10.0);
		add("eq(%s,%s)",      +[](S x, S y) -> bool       { return x==y; });

		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("x",               Builtins::X<MyGrammar>, 10);
		add("if_s(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if_c(%s,%s,%s)",  Builtins::If<MyGrammar,char>);
		add("flip()",          Builtins::Flip<MyGrammar>, 10.0);
		add("F(%s)",           Builtins::Recurse<MyGrammar>);
		add("Fm(%s)",          Builtins::MemRecurse<MyGrammar>);
	}
} grammar; 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar,&grammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	
	static double regenerate_p;
		
	double compute_single_likelihood(const datum_t& x) override {	
		
		// This would be a normal call:
		const auto out = call(x.input, ""); 
		const auto outZ = out.Z();

		// Or we can call and get back a list of completed virtual machine states
		// these store a bit more information, like runtime counts and haven't computed the
		// marginal probability of strings
//		auto v = call_vms(x.input, "<err>", this); // return the states instead of the marginal outputs
//		auto z = logsumexp(v, +[](const VirtualMachineState_t& a) { return a.lp;});
//		for(auto& vi : v) {
//			prior += -exp(vi.lp-z)*vi.runtime_counter.total; // compute a "runtime" prior penalty NOTE this is fine since a Bayesable first computes the prior before the likelihood so this will not be overwritten
//		}
//		auto out = marginal_vms_output(v); // and convert v to a disribution on strings

		const auto log_A = log(alphabet.size());
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		double lp = -infinity;
		for(auto& [s,slp] : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, (slp-outZ) + p_delete_append<strgamma,strgamma>(s, x.output, log_A));
		}
		
		return lp;
	}
	
	/**
	 * @brief We'll define a custom proposer here with a global static variable that says how much we 
	 *        regenerate vs insert/delete (defaultly regenerate_p=0.5)
	 * @return 
	 */
	
	[[nodiscard]] virtual std::pair<MyHypothesis,double> propose() const override {
		
		std::pair<Node,double> x;
		if(flip(regenerate_p)) {
			x = Proposals::regenerate(&grammar, value);	
		}
		else {
			if(flip()) x = Proposals::insert_tree(&grammar, value);	
			else       x = Proposals::delete_tree(&grammar, value);	
		}
		
		return std::make_pair(MyHypothesis(std::move(x.first)), x.second); 
	}	
	
	void print(std::string prefix="") override {
		// we're going to make this print by showing the language we created on the line before
		prefix = prefix+"#\n#" +  this->call("", "<err>").string() + "\n";
		prefix = prefix+str(int(getpid()))+"\t"+str(FleetStatistics::global_sample_count)+"\t";
//		prefix += str(grammar->compute_enumeration_order(value)) + "\t"; 
		Super::print(prefix); 
	}
};

// Probability of regenerating
double MyHypothesis::regenerate_p = 0.75;

////////////////////////////////////////////////////////////////////////////////////////////

// if we define DO_NOT_INCLUDE_MAIN then we can import everything *except* the below
#ifndef DO_NOT_INCLUDE_MAIN

#include "TopN.h"
#include "MCMCChain.h"
#include "ChainPool.h"
#include "ParallelTempering.h"
#include "Fleet.h" 
#include "Builtins.h"
#include "VMSRuntimeError.h"

#include "HillClimbing.h"


int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	//------------------
	// Add the terminals to the grammar
	//------------------	
	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 10.0/alphabet.length());
	}
		
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

	top.print_best = true;
	auto h0 = MyHypothesis::sample();
	ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 1.20);
//	ChainPool samp(h0, &mydata, FleetArgs::nchains);
//	MCMCChain samp(h0, &mydata);
//	HillClimbing samp(h0, &mydata);

	for(auto h : samp.run(Control()) | top | print(FleetArgs::print) | thin(FleetArgs::thin)) {
		UNUSED(h);
	
//		CERR h.born_chain_idx TAB h.string() ENDL;
//	
//		if(idx++ % 10000 == 1) {
//			samp.show_statistics();
//		}

	}
	top.print();

}

#endif