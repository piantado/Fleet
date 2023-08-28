/*! \mainpage Fleet - Fast inference in the Language of Thought
 *
 * \section intro_sec Introduction
 *
 * Fleet is a C++ library for programming language of thought models. In these models, we  specify a grammar of primitive 
 * operations which can be composed to form complex hypotheses. These hypotheses
 * are best thought of as programs in a (mental) programming language, and the job of learners is to observe
 * data (typically inputs and outputs of programs) and infer the most likely program to have generated the outputs
 * from the inputs. This is accomplished in Fleet by using a fully-Bayesian setup, with a prior over programs typically
 * defined thought a Probabilistic Context-Free Grammar (PCFG) and a likelihood model that typically says that the 
 * output of a program is observed with some noise. 
 * 
 * Fleet is most similar to LOTlib (https://github.com/piantado/LOTlib3) but is considerably faster. LOTlib converts
 * grammar productions into python expressions which are then evaled in python; this process is flexible and powerful, 
 * but slow. Fleet avoids this by implementing a lightweight stack-based virtual machine in which programs can be 
 * directly evaluated. This is especially advantageous when evaluating stochastic hypotheses (e.g. those using flip() or 
 * sample()) in which multiple execution paths must be evaluated. Fleet stores these multiple execution traces of a single
 * program in a priority queue (sorted by probability) and allows you to rapidly explore the space of execution traces. 
 * 
 * Fleet is structured to automatically create this virtual machine and a grammar for programs from just the type
 * specification on primitives: a function signature yields a PCFG because the return type can be thought of as a nonterminal
 * in a PCFG and its arguments can be thought of as children (right hand side of a PCFG rule). A PCFG expanded in this way 
 * will yield a correctly-typed expression, a program, where each function has arguments of the correct types. Note that in
 * Fleet, these types in the PCFG must be C++ types (intrinsics, classes, or structs), and two types that are the same
 * are always considered to have the same nonterminal type. 
 * 
 * Fleet requires you to define a Grammar which includes *all* of the nonterminal types that are used anywhere in the grammar.
 * Fleet makes heavy use of template metaprogramming, which prevents you from ever having to explicitly write the grammar
 * itself, but it does implicitly construct a grammar using these operations. 
 * 
 * In addition, Fleet has a number of built-in operations, which do special things to the virtual machine. These include
 * Builtins::Flip, which stores multiple execution traces; Builtins::If which uses short-circuit evaluation; 
 * Builtins::Recurse, which handles recursives hypotheses; and Builtins::X which provides the argument to the expression.
 * 
 * \subsection subsec_install Installation
 * 
 * Fleet is based on header-files, and requires no additional dependencies. Command line arguments are processed in CL11.hpp,
 * which is included in src/dependencies/. 
 * 
 * The easiest way to begin using Fleet is to modify one of the examples. For simple rational-rules style inference, try
 * Models/RationalRules; for an example using stochastic operations, try Models/FormalLanguageTheory-Simple. 
 * 
 * Fleet is developed using GNU g++ 10 and requires C++-20.
 * 
 * \subsection subsec_examples Examples
 * 
 * Fleet contains several implemented examples of existing program-induction models, including identical or close variants of:
 * 
 * 	- The rational rules model (Examples/RationalRules): Goodman, N. D., Tenenbaum, J. B., Feldman, J., & Griffiths, T. L. (2008). A rational analysis of rule‐based concept learning. Cognitive science, 32(1), 108-154.
 *  - The number model (Examples/Number-Simple): Piantadosi, S. T., Tenenbaum, J. B., & Goodman, N. D. (2012). Bootstrapping in a language of thought: A formal model of numerical concept learning. Cognition, 123(2), 199-217.
 *  - The number game (Examples/NumberGame): Tenenbaum, J. B. (1999). A Bayesian framework for concept learning (Doctoral dissertation, Massachusetts Institute of Technology).
 *  - Inference of a grammar (Examples/GrammarInference-*): Piantadosi, S. T., Tenenbaum, J. B., & Goodman, N. D. (2016). The logical primitives of thought: Empirical foundations for compositional cognitive models. Psychological review, 123(4), 392.
 *  - Formal language theory learning (Examples/FormalLanguageTheory-*): Yang & Piantadosi, forthcoming
 * 
 * as well as several other examples, including sorting and first order logical theories. 
 * 
 * \subsection subsec_style Style
 * 
 * Fleet is currently written using a signle-file, header-only style. This allows for rapid
 * refactoring and prototyping since declarations and implementations are not in separate files. This style may change in the future
 * because it also leads to slower compilation times. 
 * 
 * \subsection subsec_inference Inference
 * 
 * Fleet provides a number of simple inference routines to use. Examples of each can be found in Models/Sorting
 * 
 * - Markov-Chain Monte-Carlo
 * - MCMC With Parallel Tempering
 * - Monte-Carlo Tree search 
 * - Beam search
 * - Enumeration (with a fast, fancy implementation)
 * 
 * Generally, we find that ParallelTempering is the fastest and most effective way to search these spaces of programs. These can 
 * be compared in Models/Sorting, which has examples of each.  
 * 
 * 
 * \section tutorial_sec Tutorial 
 * 
 * To illustrate how Fleet works, let's walk through a simple example: the key parts of FormalLanguageTheory-Simple. This is
 * a program induction model which takes a collection of strings and tries to find a stochastic program which can explain
 * the observed strings. For example, it might see data like {"ab", "abab", "ababab"} and then infer that the language is 
 * (ab)^n. 
 * 
 * Let's first walk through the grammar definition. Here, we first import Grammar.h and also Singleton.h. Singleton is a 
 * design pattern that permits only one copy of an object to be constructed, which is used here to ensure that we only
 * have one grammar (and aren't, e.g. passing copies of it around). Typical to Fleet's code, we define our own grammar as
 * MyGrammar. The first two template arguments are the input and output types of the function we are learning (here, strings to
 * strings) and the next two are (variadic) template arguments for all of the nonterminal types used by the grammar (here, 
 * strings and bools). The input type determines the type of any arguments to productions in the grammar; the output type
 * determines the type of the root (e.g. what evaluating a program generated from this grammar will return). 
 * 
 * \code{.cpp}

#include <string>
using S = std::string; // just for convenience
 
#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,  S,bool>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
	    // compute the tail of a string: tail(wxyz) -> xyz
		add("tail(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : s.substr(1,S::npos)); });

	    // compute the head of a string: tail(wxyz) -> w
		add("head(%s)",      +[](S s)      -> S { return (s.empty() ? S("") : S(1,s.at(0))); });

	    // concatenate strings pair(wx,yz) -> wxyz
		add("pair(%s,%s)",   +[](S a, S b) -> S { 
			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError(); // caught inside VirtualMachineState::run
			else                     				 return a+b; 
		});

		// the empty string
		add("\u00D8",        +[]()         -> S          { return S(""); });
		  
		// check if two strings are equal
		add("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; });


		// logical operations
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		
		// access the argument x to the program (defined to a string type above)
		add("x",             Builtins::X<MyGrammar>);
		 
		// random coin flip (this does something fancy in evaluation)
		add("flip()",        Builtins::Flip<MyGrammar>, 10.0);
		 
		// recursion
		add("recurse(%s)",   Builtins::Recurse<MyGrammar>);
		
		// add all the characters in our alphabet (alphabet is global variable defined on the command line)
		for(const char c : alphabet) {
			add_terminal( Q(S(1,c)), S(1,c), 5.0/alphabet.length());
		}
		
	}
} grammar;
 * \endcode
 * 
 * Then, in the constructor MyGrammar(), we call the member function Grammar::add which takes two arguments: a string 
 * for how to show the function (called a "format"), and a lambda expression for the function itself. The string formats are
 * just for show -- they have nothing to do with how the program is evaluated and expressions in these strings are never
 * interpreted as code. Each string format uses "%s" to denote the string of its children. So, when Fleet is asked to display
 * a program, it recursively substitutes into these format strings. Fleet checks for you that you have as many "%s"es as you 
 * have arguments to each function and will complain if it's wrong. (NOTE: if you need to display children in a different
 * ordered sequence, Fleet also supports this via "%1", "%2", etc.). 
 * 
 * The second argument to Grammar::add is a lambda expression which computes and returns the value of each function (e.g. for 
 * "tail(%s)" it compute the tail of a string -- everything except the first character. 
 * Note that this function, as is generally the case in Fleet, attempts to be relatively fault-tolerant rather than throwing exceptions
 * because that's faster. In this case, we just define the tail of an empty string to be the empty string, rather than an error. 
 * 
 * The functions for computing the head of a string is defined similarly. The "pair(%s,%s)" function is meant to concatenate strings
 * but it actually needs to do a little checking because otherwise it's very easy to write programs which create exponentially
 * long strings. So this function checks if its two argument lengths are above a global variable MAX_LENGTH and if so it throws a
 * VMSRuntimeError (VirtualMachineState error) which is caught and handled internally in Fleet. (Note that in the actual Model code, 
 * there is a more complex version of pair which is a bit faster, as it modifies strings stored in the VirtualMachineState stack rather
 * than passing them around, as here). 
 *
 * Then, we have a function which takes no arguments and only returns the empty string. Here, we have given it a Unicode name \u00D8
 * which will appear/render in most terminals as epsilon (a convention for the empty string in formal language theory). 
 * 
 * The "(%s==%s)" function computes whether two strings are equal and returns a bool. Note that generally it is helpful to enclose
 * expressions like this in parentheses (i.e. "(...)") because without this, the printed programs can be ambiguous. 
 * 
 * Note that in each of these lambda expressions, the input types (e.g. "S a, S b" in "pair") and return type (S) really matter.
 * Fleet uses some fancy template magic to extract these types from the functions themselves and build the required grammar. That
 * allows it to start with MyGrammar's return type and sample a composition of functions which yield this type. 
 * 
 * MyGrammar also adds built-in logical operations. Builtins are functions that we do *not* define ourselves using lambdas. We certainly
 * could, but these are buitlin because they are a bit fancier than what you might think. 
 * Specifically, in most computer science applications we want the versions of these functions which involve
 * <a href="https://en.wikipedia.org/wiki/Short-circuit_evaluation">short circuit evaluation</a>. 
 * For example, and(x,y) won't evaluate y if x is false, which for us ends up with a measurably
 * faster implementation. This is maybe most important though for "if(%s,%s,%s)", where we only want to evaluate one branch, depending
 * on which way the boolean comes out. The implementation of correct short-circuit evaluation depends on some of the internals of 
 * Fleet and so it's best to use the built-in versions of these functions. 
 * 
 * We also have a special fucntion Builtins::X which provides the argument to the function/program we are learning. We defaulty
 * give this the name "x".
 * 
 * The "flip()" primitive is somewhat fancy (and Buillt-in) because it is a stochastic operation, which acts like a coin flip, 
 * returning true half of the time and false half of the time (Fleet also a built-in biased coin, which takes a coin weight as a
 * parameter). This is fancy because it means that when the program evaluates, there is no longer one single output, but rather
 * a distribution of possible return values. Fleet will hand these back to you as a DiscreteDistribution of the output type of the
 * function. Here, we have included 10.0 after the Builtins::Flip. This is an optional number which can be included for any primitive 
 * and it determines the probability in the PCFG of each each expansion (they are summed and renormalized for each nonterminal type).
 * In this case, we need to upweight the probability of Flip in order to make a proper PCFG (e.g. one where no probabliity is given to
 * infinite trees) because otherwise the and,or,not operatiosn above will lead to infinite trees. The default weight for all operations 
 * (when its not specified) is 1.0, meaning that 10.0 makes it ten times as likely that a bool will expand to a flip than the others. 
 * In general, you will want to upweight the terminal rules (those with no children) in order to keep grammars proper. 
 * 
 * The next operation is Builtins::Recurse, which allows a defined function to call itself recursively. This is a special Builtin because
 * it requires us to evaluate the entire program in which it occurs again, and use the output of that program. Note that in general
 * as we search over programs, we will find things that recurse infinitely -- Fleet's VirtualMachineState keeps track of how many
 * recursive calls a single program has made (via VirtualMachineState::recursion_depth) and when it exceeds VirtualMachineControl::MAX_RECURSE
 * or when the total runtime exceeds VirtualMachineControl::MAX_RUN_PROGRAM, Fleet will halt evaluation of the program. 
 * 
 * Finally, this grammar adds one terminal rule for each character in the global string "alphabet." Here, these characters are 
 * converted to strings containing a single character because char is not a nonterminal type in the grammar we defined. The entire
 * collection of characters is given an unnonormalized grammar probabiltiy of 5.0. 
 * 
 * Next, we define a class, MyHypothesis, which defines the program hypotheses we are going to be learning. The simplest form for
 * this is to use something of type LOTHypothesis, which defines built-in  functions for computing the prior, copying, proposing, etc.
 * \code{.cpp}
 
#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar,&grammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const datum_t& x) override {	
	 
	    // MyHypothesis stores a program, and "call" will call it with a given input
		// The output is a DiscreteDistribution on all of the outputs of that program
		const auto out = call(x.input, ""); 
		  
		// we need this to compute the likelihood
		const auto log_A = log(alphabet.size());
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		// So this requires us to sum over all of the output values, for a single data point. 
		double lp = -infinity;
		for(auto& o : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, o.second + p_delete_append<strgamma,strgamma>(o.first, x.output, log_A));
		}
		return lp;
	}
	
	void print(std::string prefix="") override {
		// Override the default "print" function by prepending on a view of the distribution 
		// that was output (Fleet normally would just show the program)
		prefix = prefix+"#\n#" +  this->call("", "<err>").string() + "\n";
		Super::print(prefix); 
	}
};  
 * \endcode
 *  
 * Here, MyHypothesis uses the 
 * <a href="https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern">curiously recurring template pattern</a> to pass itself, so 
 * that it knows how to make copies of itself. The other template arguments are the input and output types of its function (string to string) 
 * and the Grammar type it uses (MyGrammar). The address of the grammar itself &grammar is also part of the type.
 * Then, we inherit LOTHypothesis' constructors (via Super::Super's <a href="https://en.wikipedia.org/wiki/C%2B%2B11#Object_construction_improvement">insanity</a> in C++).
 *
 *  Primarily, what we have to define
 * here is the compute_single_likelihood function, which defines the likelihood of a single observed data string. To compute this, we first
 * call the function (LOTHypothesis::call) on the observed input. This returns a DiscreteDistribution<std::string> of outputs. Note that the
 * outputs which return errors are mapped to the second argument to call, in this case the empty string. Then, we loop over outputs and add up
 * the likelihood of the observed output x.ouput given the program output o.first weighted by the program's probability of that output o.second. 
 * This part uses p_delete_append, which is a string edit likelihood that assigns nonzero probability of any string being corrupted to any other. 
 * It computes this by imagining that the output string was corrupted by noise that deleted from the end of the string, and then appended, where
 * both operations take place with geometric probabilities. These probabilities are passed as template arguments since that lets us compute
 * some key pieces of this at compile time, yielding much faster code (this is a hot part of any string-based inference model).
 * 
 * We also define a print function which allows us to say how we print out the program hypothesis. Defaultly, any LOTHypothesis will just 
 * print (using the format strings like "pair(%s,%s)" above) but here we would like to also show the distribution of outputs when we print,
 * so this prints a line with a LOTHypothesis::call (which is a DiscreteDistribution) and then a .string() call on the returned value. 
 * 
 * The last piece of code is the actual running: Here, we define a Fleet object, which basically manages the input/output. We tell it
 * that we want to be able to specify the alphabet and a string for the data (comma separated strings) on the command line. We create a grammar
 * and note that this must happend after Fleet.initialize, since creating the grammar object needs to know the alphabet. Then, we create a 
 * TopN object to store the top hypotheses we have found. This is basically a sorted collection of the best hypotheses found so far. We set
 * a few control aspects of our VirtualMachine (e.g. how many steps we should run each program for, how many outputs we will consider for 
 * each program). We convert our datastr (specified on the command line) to type MyHypothesis::data_t, and do a check on whether the data contains
 * anything not in the alphabet. 
 * 
 * \code{.cpp}
 
#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 
#include "Builtins.h"
#include "VMSRuntimeError.h"

// define this so we can use it
std::string alphabet = "abcd";
  
std::string datastr = "a:aabb,c:ccbb"; // input-output pairs in our observed data
 
int main(int argc, char** argv){ 
	
	// define a Fleet object
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	// top stores the top hypotheses we have found. We set the number of top to store
	// via the command line --top=100. Internally, that sets FleetArgs::top, which is used
	// as the constructor for TopN. So it only works after you call Fleet.initialize above.
	TopN<MyHypothesis> top;
	
	// mydata stores the data for the inference model
	// Note that Fleet has these nice string_to functions which will parse strings in a standard
	// format into many of its standard data types. Here, MyHypothesis::data_t is a comma-separated
	// list of MyHypothesis::datum_t's, and each of those is an input:output pair using the 
	// alphabet
	auto mydata = string_to<MyHypothesis::data_t>(datastr);
	
	// let's just check that everything in the data is in the alphabet
	for(const auto& di: mydata) {
		for(char c: di.output) { // only checking the outputs are all in the alphabet
			assert(contains(alphabet,c));
		}
	}

	// tell Fleet's top to print each best hypothesis it finds 
	// (this can be set with the command line --print-top-best=1)
	top.print_best = true;
	 
	// Create a hypothesis to start our MCMC chain on
	auto h0 = MyHypothesis::sample();
	 
	// Create an MCMC chain
	MCMCChain c(h0, &mydata, top);
	 
	// Actually run -- note this uses C++'s new coroutines (like python generators)
	// the | printer(FleetArgs::print) here will print out every FleetArgs::print steps
	// (which is set via --print=1000). Defaultly FleetArgs::print=0, which does not print. 
	for(auto& h : c.run(Control() | printer(FleetArgs::print) ) {
		top << h; // add h to top
		 
		// we can do other stuff with h in here if we want to. 
	}

	// at the end, print the hypotheses we found
	top.print();
}
  
 *  \endcode
 * 
 * And finally, we run the actual model. We make an initial hypothesis using MyHypothesis::sample, which creates a random hypothesis from the prior,
 * we make an MCMCChain, and we run it. This uses C++'s fancy coroutines to let us process the samples as a loop. The c.run
 * function takes a Control object, which basically specifies the number of steps to run or amount of time to run etc. 
 * When Control() is called with no arguments, it gets its arguments from the command line. This means that automatically, we can give a
 * commandline arugment like "--time=30s" and this fleet program will run for 30 seconds (times can be in days (d), hours (h), minutes (m), 
 * seconds (s) or miliseconds (q).
 * 
 * Note that there are several inference schemes, and most (including the examples in Models) use ParallelTempering, which seems to work best.
 * 
 * As the chain runs, it will place hypotheses into "top" (which defaultly reads the command line argument (e.g. "--top=25") to figure out
 * how many of the best hypotheses to store. At the end of this code, we call top.print(), which will call the .print() function on everything
 * in top and show them sorted by posterior probability score (remember above we overwrote MyHypothesis::print in order to have it run the 
 * program). 
 * 
 * At the end of this program, as the Fleet object is destroyed, it will print out some statistics about the total number of MCMC steps, total
 * runtime, etc. (This can be turned off by setting FleetArgs::print_header=false). 
 *
 */

#pragma once 

#include <filesystem>
#include <sys/resource.h> // just for setting priority defaulty 
#include <unistd.h>
#include <stdlib.h>
#include <csignal>
#include <string>

// For macs:
#ifdef __APPLE__
    #include <sys/syslimits.h> // used to get PATH_MAX
    #include <mach-o/dyld.h> // dynamic link editor with method to get executable path
#endif


const std::string FLEET_VERSION = "0.1.4";

#include "Random.h"
#include "Timing.h"
#include "Control.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Fleet global variables
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "FleetArgs.h"

unsigned long random_seed  = 0;

// This is global that checks whether CTRL_C has been pressed
// NOTE: this must be registered in main with signal(SIGINT, Fleet::fleet_interrupt_handler);
volatile sig_atomic_t CTRL_C = false;

// apparently some OSes don't define this
#ifndef HOST_NAME_MAX
	const size_t HOST_NAME_MAX = 256;
#endif
char hostname[HOST_NAME_MAX]; 	

// store the main thread id if we want it
std::thread::id main_thread_id = std::this_thread::get_id();

#include "FleetStatistics.h"

/**
 * @class Fleet
 * @author piantado
 * @date 25/05/20
 * @file Fleet.h
 * @brief A fleet object manages processing the command line, prints a useful summary of command lines, and then on its destruction (or
 * 		  calling completed(), it prints out a summary of the total number of samples etc. run per second. This provides a shallow
 *        wrapper to CLI::App, meaning you add options to it just like CLI::App
 */
class Fleet {
public:
	CLI::App app;
	timept start_time;
	bool done; // if this is true, we don't call completed on destruction
	
	Fleet(std::string brief) : app{brief}, done(false) {

		app.add_option("-S,--seed",      random_seed, "Seed the rng (0 is no seed)");
		app.add_option("-s,--steps",     FleetArgs::steps, "Number of MCMC or MCTS search steps to run");
		app.add_option("--inner-steps",  FleetArgs::inner_steps, "Steps to run inner loop of a search");
		app.add_option("--burn",         FleetArgs::burn, "Burn this many steps");
		app.add_option("--thin",         FleetArgs::thin, "Thinning on callbacks");
		app.add_option("-p,--print",     FleetArgs::print, "Print out every this many");
		app.add_option("-t,--top",       FleetArgs::ntop, "The number to store");
		app.add_option("-n,--threads",   FleetArgs::nthreads, "Number of threads for parallel search");
		app.add_option("--explore",      FleetArgs::explore, "Exploration parameter for MCTS");
		app.add_option("--restart",      FleetArgs::restart, "If we don't improve after this many, restart a chain");
		app.add_option("-c,--chains",    FleetArgs::nchains, "How many chains to run");
		app.add_option("--ct",           FleetArgs::chainsthreads, "Specify chains and threads at the same time");
		
		app.add_option("--partition-depth",   FleetArgs::partition_depth, "How deep do we recurse when we do partition-mcmc?");
				
		app.add_option("-o,--output",      FleetArgs::output_path, "Where we write output");
		app.add_option("--input",          FleetArgs::input_path, "Read standard input from here");
		app.add_option("-T,--time",        FleetArgs::timestring, "Stop (via CTRL-C) after this much time (takes smhd as seconds/minutes/hour/day units)");
		app.add_option("--inner-restart",  FleetArgs::inner_restart, "Inner restart");
		app.add_option("--inner-time",     FleetArgs::inner_timestring, "Inner time");
		app.add_option("--inner-thin",     FleetArgs::inner_thin, "Inner thinning");
		app.add_option("--tree",           FleetArgs::tree_path, "Write the tree here");

		app.add_option("--omp-threads",    FleetArgs::omp_threads, "How many threads to run with OMP");
		
		app.add_option("--header",         FleetArgs::print_header, "Set to 0 to not print header");
				
		app.add_flag("--top-print-best",   FleetArgs::top_print_best, "Should all tops defaultly print their best?");
		app.add_flag("--print-proposals",  FleetArgs::print_proposals, "Should we print out proposals?");
		
//		app.add_flag(  "-q,--quiet",    quiet, "Don't print very much and do so on one line");
//		app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");

		start_time = now();
		
		// now if we are running MPI
//		#ifdef FLEET_MPI
		
//		#endif
		
	}
	
	/**
	 * @brief On destructor, we call completed unless we are already done
	 */	
	virtual ~Fleet() {
		if(not done) completed();
	}
	
	///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	/// A Handler for CTRL_C
	///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


	static void fleet_interrupt_handler(int signum) { 
		if(signum == SIGINT) {
			CTRL_C = true; 
		}
		else if(signum == SIGHUP) {
			// do nothing -- defaultly Fleet mutes SIGHUP so that commands left in the terminal will continue
			// this is so that if we get disconnected on a terminal run, the job is maintained
		}	
	} 
	
	
	template<typename T> 
	void add_option(std::string c, T& var, std::string description ) {
		app.add_option(c, var, description);
	}
	template<typename T> 
	void add_flag(std::string c, T& var, std::string description ) {
		app.add_flag(c, var, description);
	}
		
	int initialize(int argc, char** argv) {
		
		//CLI11_PARSE(app, argc, argv);
		app.parse(argc, argv); // parse here -- and DO NOT catch exit codes (unlike the macro above)
							   // if we catch exit codes, we silently keep running without parsing...
		
		// set our own handlers -- defaulty HUP won't stop
		std::signal(SIGINT, Fleet::fleet_interrupt_handler);
		std::signal(SIGHUP, Fleet::fleet_interrupt_handler);
		
		// give us a defaultly kinda nice niceness
		setpriority(PRIO_PROCESS, 0, 5);
		
		// convert everything to ms
		FleetArgs::runtime = convert_time(FleetArgs::timestring);	
		FleetArgs::inner_runtime = convert_time(FleetArgs::inner_timestring);


		// 
		if( FleetArgs::chainsthreads != 0) {
			assert((FleetArgs::nchains == 1 and FleetArgs::nthreads==1) && "*** Cannot specify chains or threads with --ct");
			FleetArgs::nchains  = FleetArgs::chainsthreads;
			FleetArgs::nthreads = FleetArgs::chainsthreads;
		}

		// seed the rng
		DefaultRNG.seed(random_seed);

		if(FleetArgs::print_header) {
			// Print standard fleet header
			gethostname(hostname, HOST_NAME_MAX);
			
			#ifdef __APPLE__
			     char path[PATH_MAX+1]; 
				uint32_t size = sizeof(path); 
				_NSGetExecutablePath(path, &size); 
				char tmp[128]; sprintf(tmp, "md5sum %s", path);
			#else 
			
				// and build the command to get the md5 checksum of myself
				char tmp[128]; sprintf(tmp, "md5sum /proc/%d/exe", getpid());
			#endif
				
			std::filesystem::path cwd = std::filesystem::current_path();
			std::string exc = system_exec(tmp); exc.erase(exc.length()-1); // came with a newline?
			
			print("# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
			print("# Running Fleet on " +  std::string(hostname) + " with PID=" + str(getpid()) + " by user " + getenv("USER") + " at " + datestring());
			print("# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
			print("# Fleet version: ", FLEET_VERSION);
			print("# Executable checksum: ", exc);
			print("# Path:", cwd.string() );
			print("# Run options: ");
			for(int a=0;a<argc;a++) {
				print("#\t", argv[a]);
			}
			print("# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");	
			
		}
		
		// give warning for infinite runs:
		if(FleetArgs::steps == 0 and FleetArgs::runtime==0) {
			CERR "# Warning: you haven not specified --time or --steps so this will run forever or until CTRL-C." ENDL;
		}
		
		return 0;
	}
	
	
	void completed() {
		
		if(FleetArgs::print_header) {
			auto elapsed_seconds = time_since(start_time) / 1000.0;
			
			COUT "# Elapsed time:"        TAB elapsed_seconds << " seconds " ENDL;
			if(FleetStatistics::global_sample_count > 0) {
				COUT "# Samples per second:"  TAB FleetStatistics::global_sample_count/elapsed_seconds ENDL;
				COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
			}
			if(FleetStatistics::beam_steps > 0) {
				COUT "# Total Beam Search steps:" TAB FleetStatistics::beam_steps ENDL;
			}
			if(FleetStatistics::enumeration_steps > 0) {
				COUT "# Total enumeration steps:" TAB FleetStatistics::enumeration_steps ENDL;
			}		
			if(FleetStatistics::depth_exceptions > 0){
				COUT "# Warning: " TAB FleetStatistics::depth_exceptions TAB " grammar depth exceptions." ENDL;
			}
			
			COUT "# Total posterior calls:" TAB FleetStatistics::posterior_calls ENDL;
			COUT "# Millions of VM ops per second:" TAB (FleetStatistics::vm_ops/1000000)/elapsed_seconds ENDL;
			
			COUT "# Fleet completed at " <<  datestring() ENDL;
			
		}
		
		// setting this makes sure we won't call it again
		done = true;
		
		// HMM CANT DO THIS BC THERE MAY BE MORE THAN ONE TREE....
		//COUT "# MCTS tree size:" TAB m.size() ENDL;	
		//COUT "# Max score: " TAB maxscore ENDL;
		//COUT "# MCTS steps per second:" TAB m.statistics.N ENDL;	
	}
	
};

