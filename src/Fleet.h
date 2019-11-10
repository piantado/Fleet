/* Globally shared variables and declarations */

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
#include <mutex>

#include "dependencies/CL11.hpp"

#include <sys/resource.h> // just for setting priority defaulty 

const std::string FLEET_VERSION = "0.0.7";

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
	const size_t MAX_CHILD_SIZE = 32; // rules can have at most this many children 

}
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// A Handler for CTRL_C
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// NOTE: this must be registered in main with signal(SIGINT, fleet_interrupt_handler);
#include <signal.h>
volatile sig_atomic_t CTRL_C = false;
void fleet_interrupt_handler(int signum) { 
	if(signum == SIGINT) {
		CTRL_C = true; 
	}
	else if(signum == SIGHUP) {
		// do nothing -- defaultly Fleet mutes SIGHUP so that commands left in the terminal will continue
		// this is so that if we get disconnected on a terminal run, the job is maintained
	}	
} 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These are standard variables that occur nearly universally in these searches
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

const std::string ChildStr = "%s"; // how do strings get substituted?

unsigned long random_seed  = 0;
unsigned long mcts_steps   = 0;
unsigned long mcmc_steps   = 0; 
unsigned long thin         = 0;
unsigned long ntop         = 100;
unsigned long mcmc_restart = 0;
unsigned long checkpoint   = 0; 
double        explore      = 1.0; // we want to exploit the string prefixes we find
size_t        nthreads     = 1;
unsigned long runtime      = 0;
unsigned long nchains      = 1;
bool          concise      = false; // this is used to indicate that we want to not print much out (typically only posteriors and counts)
std::string   input_path   = "input.txt";
std::string   tree_path    = "tree.txt";
std::string   output_path  = "output.txt";
std::string   timestring   = "0s";


namespace Fleet { 	
	CLI::App DefaultArguments(const char* brief) {
		CLI::App app{brief};
		
		app.add_option("-R,--seed",    random_seed, "Seed the rng (0 is no seed)");
		app.add_option("-s,--mcts",    mcts_steps, "Number of MCTS search steps to run");
//		app.add_option("-S,--mcts-scoring",  mcts_scoring, "How to score MCTS?");
		app.add_option("-m,--mcmc",     mcmc_steps, "Number of mcmc steps to run");
		app.add_option("-t,--thin",     thin, "Thinning on the number printed");
		app.add_option("-o,--output",   output_path, "Where we write output");
		app.add_option("-O,--top",      ntop, "The number to store");
		app.add_option("-n,--threads",  nthreads, "Number of threads for parallel search");
		app.add_option("-e,--explore",  explore, "Exploration parameter for MCTS");
		app.add_option("-r,--restart",  mcmc_restart, "If we don't improve after this many, restart");
		app.add_option("-i,--input",    input_path, "Read standard input from here");
		app.add_option("-T,--time",     timestring, "Stop (via CTRL-C) after this much time (takes smhd as seconds/minutes/hour/day units)");
		app.add_option("-E,--tree",     tree_path, "Write the tree here");
		app.add_option(  "-c,--chains",   nchains, "How many chains to run");
		
		app.add_flag(  "-q,--concise",  concise, "Don't print very much and do so on one line");
//		app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");

		return app; 
	}
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "Stack.h"
typedef Stack<Instruction> Program;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We defaultly include all of the major requirements for Fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "Numerics.h"
#include "Random.h"
#include "Strings.h"
#include "Hash.h"
#include "Miscellaneous.h"

#include "Interfaces/Dispatchable.h"
#include "Interfaces/Bayesable.h"
#include "Interfaces/MCMCable.h"
#include "Interfaces/Searchable.h"

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

	// give us a defaultly nice niceness
	setpriority(PRIO_PROCESS, 0, 19);

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

	// Get the start time
    auto timenow = std::chrono::system_clock::to_time_t(now()); 

	// and build the command to get the md5 checksum of myself
	char tmp[64]; sprintf(tmp, "md5sum /proc/%d/exe", getpid());
	
	// parse the time
	runtime = convert_time(timestring);	
	
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Running Fleet on " << hostname << " with PID=" << getpid() << " by user " << username << " at " << ctime(&timenow);
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Executable checksum: " << system_exec(tmp);
	COUT "# \t --input=" << input_path ENDL;
	COUT "# \t --threads=" << nthreads ENDL;
	COUT "# \t --chains=" << nchains ENDL;
	COUT "# \t --mcmc=" << mcmc_steps ENDL;
	COUT "# \t --mcts=" << mcts_steps ENDL;
	COUT "# \t --time=" << timestring << " (" << runtime << " seconds)" ENDL;
	COUT "# \t --restart=" << mcmc_restart ENDL;
	COUT "# \t --seed=" << random_seed ENDL;
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;	
}
