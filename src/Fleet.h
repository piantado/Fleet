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
 * Builtin::Flip, which stores multiple execution traces; Builtin::If which uses short-circuit evaluation; 
 * Builtin::Recurse, which handles recursives hypotheses; and Builtin::X which provides the argument to the expression.
 * 
 * \section install_sec Installation
 * 
 * Fleet is based on header-files, and requires no additional dependencies. Command line arguments are processed in CL11.hpp,
 * which is included in src/dependencies/. 
 * 
 * The easiest way to begin using Fleet is to modify one of the examples. For simple rational-rules style inference, try
 * Models/RationalRules; for an example using stochastic operations, try Models/FormalLanguageTheory-Simple. 
 * 
 * Fleet is developed using GCC 9 (version >8 required) and requires C++-17 at present, but this may move to C++-20 in the future.
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
 * strings and bools). 
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
			if(a.length() + b.length() > MAX_LENGTH) throw VMSRuntimeError();
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
		
		// add all the characters in our alphabet (a global variable)
		for(const char c : alphabet) {
			add_terminal( Q(S(1,c)), S(1,c), 5.0/alphabet.length());
		}
		
	}
};
 * \endcode
 * 
 * Then, in the constructor MyGrammar(), we call the member function Grammar::add which takes two argumetns: a string 
 * for how to show the function, and a lambda expression for the function itself. For example, the first function computes
 * the tail of a string (everything except the first character), which shows up in printed programs as "tail(%s)" (where
 * the %s is replaced by the arguments to the function. Note that in Fleet, you must include as many %s as there are
 * arugments, meaning that all arguments must be shown. 
 * 
 * The second argument to Grammar::add is a lambda expression which computes and returns the tail of a string. Note that
 * this function, as is generally the case in Fleet, attempts to be relatively fault-tolerant rather than throwing exceptions
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
 * We then add built-in logical operations. Note that we do *not* define our own versions of these functions with lambda 
 * expressions. We certainly could, but in most computer science applications we want the versions of these functions which involve
 * <a href="https://en.wikipedia.org/wiki/Short-circuit_evaluation">short circuit evaluation</a>. 
 * For example, and(x,y) won't evaluate y if x is false, which for us ends up with a measurably
 * faster implementation. This is maybe most important though for "if(%s,%s,%s)", where we only want to evaluate one branch, depending
 * on which way the boolean comes out. The implementation of correct short-circuit evaluation depends on some of the internals of 
 * Fleet and so it's best to use the built-in versions of these functions. 
 * 
 * Next, we have a special fucntion Builtins::X which provides the argument to the function/program we are learning. We defaulty
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
 * The next operation is Builtin::Recurse, which allows a defined function to call itself recursively. 
 * 
 * Finally, this grammar adds one terminal rule for each character in the global string "alphabet." Here, these characters are 
 * converted to strings containing a single character because char is not a nonterminal type in the grammar we defined. Here, the entire
 * collection of characters is given an unnonormalized grammar probabiltiy of 5.0. 
 * 
 * Next, we define a class, MyHypothesis, which defines the program hypotheses we are going to be learning. The simplest form for
 * this is to use something of type LOTHypothesis, which defines built-in  

 * 
 * 
 * \section install_sec Inference
 * 
 * Fleet provides a number of simple inference routines to use. These are all displayed in Models/FormalLanguageTheory-Simple. 
 * 
 * \subsection step1 Markov-Chain Monte-Carlo
 * \subsection step2 Search (Monte-Carlo Tree Search)
 * \subsection step3 Enumeration 
 * etc...
 * 
 * \section install_sec Typical approach
 * 	- Sample things, store in TopN, then evaluate...
 */

#pragma once 

#include <sys/resource.h> // just for setting priority defaulty 
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

const std::string FLEET_VERSION = "0.1.01";

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
// which happens in 
volatile sig_atomic_t CTRL_C = false;

// apparently some OSes don't define this
#ifndef HOST_NAME_MAX
	const size_t HOST_NAME_MAX = 256;
#endif
char hostname[HOST_NAME_MAX]; 	

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

		app.add_option("--seed",        random_seed, "Seed the rng (0 is no seed)");
		app.add_option("--steps",       FleetArgs::steps, "Number of MCMC or MCTS search steps to run");
		app.add_option("--inner-steps", FleetArgs::inner_steps, "Steps to run inner loop of a search");
		app.add_option("--burn",        FleetArgs::burn, "Burn this many steps");
		app.add_option("--thin",        FleetArgs::thin, "Thinning on callbacks");
		app.add_option("--print",       FleetArgs::print, "Print out every this many");
		app.add_option("--top",         FleetArgs::ntop, "The number to store");
		app.add_option("-n,--threads",  FleetArgs::nthreads, "Number of threads for parallel search");
		app.add_option("--explore",     FleetArgs::explore, "Exploration parameter for MCTS");
		app.add_option("--restart",     FleetArgs::restart, "If we don't improve after this many, restart a chain");
		app.add_option("-c,--chains",   FleetArgs::nchains, "How many chains to run");
		app.add_option("--partition-depth",   FleetArgs::partition_depth, "How deep do we recurse when we do partition-mcmc?");
		
		app.add_option("--header",      FleetArgs::print_header, "Set to 0 to not print header");

		
		
		app.add_option("--output",      FleetArgs::output_path, "Where we write output");
		app.add_option("--input",       FleetArgs::input_path, "Read standard input from here");
		app.add_option("-T,--time",     FleetArgs::timestring, "Stop (via CTRL-C) after this much time (takes smhd as seconds/minutes/hour/day units)");
		app.add_option("--inner-restart",  FleetArgs::inner_restart, "Inner restart");
		app.add_option("--inner-time",  FleetArgs::inner_timestring, "Inner time");
		app.add_option("--tree",        FleetArgs::tree_path, "Write the tree here");
		
//		app.add_flag(  "-q,--quiet",    quiet, "Don't print very much and do so on one line");
//		app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");

		start_time = now();
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
		CLI11_PARSE(app, argc, argv);
		
		// set our own handlers -- defaulty HUP won't stop
		signal(SIGINT, Fleet::fleet_interrupt_handler);
		signal(SIGHUP, Fleet::fleet_interrupt_handler);
		
		// give us a defaultly kinda nice niceness
		setpriority(PRIO_PROCESS, 0, 5);

		// set up the random seed:
		if(random_seed != 0) {
			rng.seed(random_seed);
		}
		else {
			rng.seed(std::random_device{}());
		}
		
		// convert everything to ms
		FleetArgs::runtime = convert_time(FleetArgs::timestring);	
		FleetArgs::inner_runtime = convert_time(FleetArgs::inner_timestring);

		if(FleetArgs::print_header) {
			// Print standard fleet header
			gethostname(hostname, HOST_NAME_MAX);

			// and build the command to get the md5 checksum of myself
			char tmp[64]; sprintf(tmp, "md5sum /proc/%d/exe", getpid());
		
			COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
			COUT "# Running Fleet on " << hostname << " with PID=" << getpid() << " by user " << getenv("USER") << " at " <<  datestring() ENDL;
			COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
			COUT "# Fleet version: " << FLEET_VERSION ENDL;
			COUT "# Executable checksum: " << system_exec(tmp);
			COUT "# Run options: " ENDL;
			COUT "# \t --input=" << FleetArgs::input_path ENDL;
			COUT "# \t --threads=" << FleetArgs::nthreads ENDL;
			COUT "# \t --chains=" << FleetArgs::nchains ENDL;
			COUT "# \t --steps=" << FleetArgs::steps ENDL;
			COUT "# \t --inner_steps=" << FleetArgs::inner_steps ENDL;
			COUT "# \t --thin=" << FleetArgs::thin ENDL;
			COUT "# \t --print=" << FleetArgs::print ENDL;
			COUT "# \t --time=" << FleetArgs::timestring << " (" << FleetArgs::runtime << " ms)" ENDL;
			COUT "# \t --restart=" << FleetArgs::restart ENDL;
			COUT "# \t --seed=" << random_seed ENDL;
			COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;	
			
		}
		
		// give warning for infinite runs:
		if(FleetArgs::steps == 0 and FleetArgs::runtime==0) {
			CERR "# Warning: you haven not specified --time or --mcmc so this will run forever or until CTRL-C." ENDL;
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
			
			
			COUT "# Total posterior calls:" TAB FleetStatistics::posterior_calls ENDL;
			COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds ENDL;
		}
		
		// setting this makes sure we won't call it again
		done = true;
		
		// HMM CANT DO THIS BC THERE MAY BE MORE THAN ONE TREE....
		//COUT "# MCTS tree size:" TAB m.size() ENDL;	
		//COUT "# Max score: " TAB maxscore ENDL;
		//COUT "# MCTS steps per second:" TAB m.statistics.N ENDL;	
	}
	
};

