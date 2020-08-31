/*! \mainpage Fleet - Fast inference in the Language of Thought
 *
 * \section intro_sec Introduction
 *
 * Fleet is a C++ library for programming language of thought models. In these models, you will typically
 * specify a grammar of primitive operations which can be composed to form complex hypotheses. These hypotheses
 * are best thought of as programs in a mental programming language, and the job of learners is to observe
 * data (typically inputs and outputs of programs) and infer the most likely program to have generated the outputs
 * from the inputs. This is accomplished in Fleet by using a fully-Bayesian setup, with a prior over programs typically
 * defined thought a Probabilistic Context-Free Grammar (PCFG) and a likelihood model that typically says that the 
 * output of a program is observed with some noise. 
 * 
 * Fleet is most similar to LOTlib (https://github.com/piantado/LOTlib3) but is considerably faster. LOTlib converts
 * grammar productions into python expressions which are then evaled in python; this process is flexible and powerful, 
 * but very slow. Fleet avoids this by implementing a lightweight stack-based virtual machine in which programs can be 
 * directly evaluated. This is especially advantageous when evaluating stochastic hypotheses (e.g. those using flip() or 
 * sample()) in which multiple execution paths must be evaluated. Fleet stores these multiple execution traces of a single
 * program in a priority queue (sorted by probability) and allows you to rapidly explore the space of execution traces. 
 * 
 * Fleet is structured to automatically create this virtual machine and a grammar for programs from just the type
 * specification on primitives. The bulk of a Fleet program is therefore in specifying the primitives that are used
 * and a likelihood model that scores any potential program against the data. 
 * 
 * To accomplish this, Fleet makes heavy use of C++ template metaprogramming. It requires strongly-typed functions
 * and requires you to specify the macro FLEET_GRAMMAR_TYPES in order to tell its virtual machine what kinds of variables
 * must be stored. In addition, Fleet uses a std::tuple named PRIMITIVES in order to help define the grammar. This tuple consists of
 * a collection of Primitive objects, essentially just lambda functions and weights). The input/output types of these primitives
 * are automatically deduced from the lambdas (using templates) and the corresponding functions are added to the grammar. Note
 * that the details of this mechanism may change in future versions in order to make it easier to add grammar types in 
 * other ways. In addition, Fleet has a number of built-in operations, which do special things to the virtual machine 
 * (including Builtin::Flip, which stores multiple execution traces; Builtin::If which uses short-circuit evaluation; 
 * Builtin::Recurse, which handles recursives hypotheses; and Builtin::X which provides the argument to the expression). 
 * These are not currently well documented but should be soon.  * 
 * 
 * \section install_sec Installation
 * 
 * Fleet is based on header-files, and requires no additional dependencies. Command line arguments are processed in CL11.hpp,
 * which is included in src/dependencies/. 
 * 
 * The easiest way to begin using Fleet is to modify one of the examples. For simple rational-rules style inference, try
 * Models/RationalRules; for an example using stochastic operations, try Models/FormalLanguageTheory-Simple. 
 * 
 * Fleet is developed using GCC 9 (version >8 required).
 *
 * \section install_sec Tutorial 
 * 
 * \code{.py}
 * code goes here
 * \endcode
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

/* Globally shared variables and declarations */

#pragma once 

#include <sys/resource.h> // just for setting priority defaulty 
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

const std::string FLEET_VERSION = "0.0.97";

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We defaultly include all of the major requirements for Fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//#include "Statistics/FleetStatistics.h"

#include "Timing.h"
#include "Control.h"

// Thie one really should be included last because it depends on Primitves
// so we will include it here 
#include "VirtualMachine/applyPrimitives.h"

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
		
		app.add_option("--header",      FleetArgs::print_header, "Set to 0 to not print header");

		
		app.add_option("--output",      FleetArgs::output_path, "Where we write output");
		app.add_option("--input",       FleetArgs::input_path, "Read standard input from here");
		app.add_option("-T,--time",     FleetArgs::timestring, "Stop (via CTRL-C) after this much time (takes smhd as seconds/minutes/hour/day units)");
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
			if(FleetStatistics::astar_steps > 0) {
				COUT "# Total A* steps:" TAB FleetStatistics::astar_steps ENDL;
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

