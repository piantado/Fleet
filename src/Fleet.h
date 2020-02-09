/* Globally shared variables and declarations */


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
 * Fleet is based on header-files, and requires no additional dependencies (command line arguments are processed in CL11.hpp,
 * which is included in src/dependencies/). 
 * 
 * The easiest way to begin using Fleet is to modify one of the examples. For simple rational-rules style inference, try
 * Models/RationalRules; for an example using stochastic operations, try Models/FormalLanguageTheory-Simple. 
 * 
 * Fleet is developed using GCC.
 *
 * \section install_sec Inference
 * 
 * Fleet provides a number of simple inference routines to use. 
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

#include "stdlib.h"
#include "stdio.h"
#include "math.h"
#include <iostream>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <vector>
#include <queue>
#include <tuple>
#include <map>
#include <atomic>
#include <assert.h>
#include <string>
#include <sstream>
#include <array>
#include <memory>
#include <pthread.h>
#include <thread>        
#include <cstdio>
#include <stdexcept>
#include <random>

#include <sys/resource.h> // just for setting priority defaulty 

const std::string FLEET_VERSION = "0.0.9";

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Tracking Fleet statistics 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

namespace FleetStatistics {
	// Running MCMC/MCTS updates these standard statistics
	
	std::atomic<uintmax_t> posterior_calls(0);	
	std::atomic<uintmax_t> hypothesis_births(0);  // how many total hypotheses have been created? -- useful for tracking when we found a solution
	std::atomic<uintmax_t> vm_ops(0);	
	std::atomic<uintmax_t> mcmc_proposal_calls(0);
	std::atomic<uintmax_t> mcmc_acceptance_count(0);
	std::atomic<uintmax_t> global_sample_count(0);
	
	void reset() {
		posterior_calls = 0;
		hypothesis_births = 0;
		vm_ops = 0;
		mcmc_proposal_calls = 0;
		mcmc_acceptance_count = 0;
		global_sample_count = 0;
	}
}

namespace Fleet { 
	size_t GRAMMAR_MAX_DEPTH = 64;
	const size_t MAX_CHILD_SIZE = 8; // rules can have at most this many children  -- for now (we can change if needed)
	
	int Pdenom = 24; // the denominator for probabilities in op_P --  we're going to enumeraet fractions in 24ths -- just so we can get thirds, quarters, fourths	
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We defaultly include all of the major requirements for Fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Timing.h"
#include "Program.h"
#include "Control.h"
#include "Numerics.h"
#include "Random.h"
#include "Strings.h"
#include "Hash.h"
#include "Miscellaneous.h"
#include "IntegerizedStack.h"

#include "Hypotheses/Interfaces/Dispatchable.h"
#include "Hypotheses/Interfaces/Bayesable.h"
#include "Hypotheses/Interfaces/MCMCable.h"
#include "Hypotheses/Interfaces/Searchable.h"

#include "Rule.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "VirtualMachine/VirtualMachineState.h"
#include "Node.h"
#include "IO.h"
#include "Grammar.h"
#include "CaseMacros.h"
#include "DiscreteDistribution.h"

#include "Hypotheses/LOTHypothesis.h"
#include "Hypotheses/Lexicon.h"

#include "Inference/MCMCChain.h"
#include "Inference/MCTS.h"
#include "Inference/ParallelTempering.h"
#include "Inference/ChainPool.h"

#include "Top.h"
#include "Primitives.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Actual initialization
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void Fleet_initialize() {
	
	// set our own handlers -- defaulty HUP won't stop
	signal(SIGINT, fleet_interrupt_handler);
	signal(SIGHUP, fleet_interrupt_handler);

	// give us a defaultly kinda nice niceness
	setpriority(PRIO_PROCESS, 0, 5);

	// set up the random seed:
	if(random_seed != 0) {
		rng.seed(random_seed);
	}

	// Print standard fleet header
	
	// apparently some OSes don't define this
	#ifndef HOST_NAME_MAX
		size_t HOST_NAME_MAX = 256;
	#endif
	#ifndef LOGIN_NAME_MAX
		size_t LOGIN_NAME_MAX = 256;
	#endif
	char hostname[HOST_NAME_MAX]; 	gethostname(hostname, HOST_NAME_MAX);
	char username[LOGIN_NAME_MAX];	getlogin_r(username, LOGIN_NAME_MAX);

	// and build the command to get the md5 checksum of myself
	char tmp[64]; sprintf(tmp, "md5sum /proc/%d/exe", getpid());
	
	// convert everything to ms
	runtime = convert_time(timestring);	
	
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Running Fleet on " << hostname << " with PID=" << getpid() << " by user " << username << " at " <<  datestring() ENDL;
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Executable checksum: " << system_exec(tmp);
	COUT "# \t --input=" << input_path ENDL;
	COUT "# \t --threads=" << nthreads ENDL;
	COUT "# \t --chains=" << nchains ENDL;
	COUT "# \t --mcmc=" << mcmc_steps ENDL;
	COUT "# \t --mcts=" << mcts_steps ENDL;
	COUT "# \t --time=" << timestring << " (" << runtime << " ms)" ENDL;
	COUT "# \t --restart=" << mcmc_restart ENDL;
	COUT "# \t --seed=" << random_seed ENDL;
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;	
}
