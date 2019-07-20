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
#include <chrono>
#include <thread>         // std::this_thread::sleep_for
#include <cstdio>
#include <stdexcept>
#include <random>
#include <mutex>

#include "CL11.hpp"

#include <sys/resource.h> // just for setting priority defaulty 

const std::string FLEET_VERSION = "0.0.5";
//const char SERIALIZATION_SEPERATOR = '~';

// First some error checking on Fleet's required macros
// this is because we use some of them in enums, and so a failure 
// to define them might cause an error that is silent or hard 
// to detect. 
#ifndef NT_NAMES
#error You must define NT_NAMES (the names of nonterminals)
#endif

#ifndef NT_TYPES
#error You must define a list NT_TYPES of types in the evaluation stack
#endif

// These are returned from the virtual machine to signal how evlauation of a program went
enum class abort_t {NO_ABORT=0, RECURSION_DEPTH, RANDOM_CHOICE, RANDOM_CHOICE_NO_DELETE, SIZE_EXCEPTION, OP_ERR_ABORT, RANDOM_BREAKOUT}; // setting NO_ABORT=0 allows us to say if(aborted)...

enum nonterminal_t {NT_NAMES, N_NTs };


// convenient to make op_NOP=0, so that the default initialization is a NOP
enum class BuiltinOp {
	op_NOP=0,op_X,op_POPX,
	op_MEM,op_RECURSE,op_MEM_RECURSE, // thee can store the index of what hte loader calls in arg, so they can be used with lexica if you pass arg
	op_FLIP,op_FLIPP,op_IF,op_JMP,
	op_TRUE,op_FALSE
};

#include "Instruction.h"

//void print(Instruction i) {	std::cout << "[" << i.is_custom << "." << i.builtin << "." << i.arg << "." << i.custom << "]"; }

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// A Handler for CTRL_C
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

namespace FleetStatistics {
	// Running MCMC/MCTS updates these standard statistics
	
	std::atomic<uintmax_t> posterior_calls(0);
	
	std::atomic<uintmax_t> hypothesis_births(0);  // how many total hypotheses have been created? -- useful for tracking when we found a solution

	std::atomic<uintmax_t> vm_ops(0);
	
	std::atomic<uintmax_t> mcmc_proposal_calls(0);
	std::atomic<uintmax_t> mcmc_acceptance_count(0);
	std::atomic<uintmax_t> global_sample_count(0);
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

std::string ChildStr = "%s"; // how do strings get substituted?

unsigned long random_seed  = 0; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long mcts_steps   = 0; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long mcmc_steps   = 0; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long thin         = 0;
unsigned long ntop         = 100;
unsigned long mcmc_restart = 0;
unsigned long checkpoint   = 0; 
double        explore      = 1.0; // we want to exploit the string prefixes we find
size_t        nthreads     = 1;
unsigned long runtime      = 0;
unsigned long chains       = 1;
bool          concise      = false; // this is used to indicate that we want to not print much out (typically only posteriors and counts)
std::string   input_path   = "input.txt";
std::string   tree_path    = "tree.txt";
std::string   output_path  = "output.txt";
std::string   timestring   = "0s";
   

namespace Fleet { 	
	CLI::App DefaultArguments() {
		CLI::App app{"Fancier number model."};
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
		app.add_flag(  "-q,--concise",  concise, "Don't print very much and do so on one line");
		app.add_flag(  "-c,--chains",   chains, "How many chains to run");
		app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");
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

#include "Hypotheses/Interfaces.h"
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

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Just a convenient wrapper for timing
/// this times between successive calls to tic()
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

auto start = std::chrono::high_resolution_clock::now();
std::chrono::duration<double> elapsed;

void tic() {
	// record the amount of time since the last tic()
	auto x = std::chrono::high_resolution_clock::now();
	elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(x-start);
	start = x;
}

double elapsed_seconds() {
	return elapsed.count(); 
}

// From https://stackoverflow.com/questions/478898/how-to-execute-a-command-and-get-output-of-command-within-c-using-posix
std::string system_exec(const char* cmd) {
    std::array<char, 1024> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Time conversions for fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

unsigned long convert_time(std::string& s) {
	// Converts our own time format to seconds, which is what Fleet's time utilities use
	// the time format we accept is #+(.#+)[smhd] where shmd specifies seconds, minutes, hours days 
	
	// specila case of s="0" will be allowed
	if(s == "0") return 0;
		
	// else we must specify a unit	
	double multiplier; // for default multiplier of 1 is seconds
	switch(s.at(s.length()-1)) {
		case 's': multiplier = 1; break; 
		case 'm': multiplier = 60; break;
		case 'h': multiplier = 60*60; break;
		case 'd': multiplier = 60*60*24; break;
		default: 
			CERR "*** Unknown time specifier: " << s.at(s.length()-1) << " in " << s << ". Did you forget a unit?" ENDL;
			assert(0);
	}
	
	double t = std::stod(s.substr(0,s.length()-1)); // all but the last character
		
	return (unsigned long)(t*multiplier); // note this effectively rounds to the nearest escond 
	
}


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Actual initialization
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void Fleet_initialize() {
	// set our own handlers
	signal(SIGINT, fleet_interrupt_handler);
	signal(SIGHUP, fleet_interrupt_handler);

	// give us a default niceness
	setpriority(PRIO_PROCESS, 0, 19);

	// Print standard fleet header
	
	// apparently some OSes don't define this
#ifndef HOST_NAME_MAX
	size_t HOST_NAME_MAX = 256;
#endif
#ifndef LOGIN_NAME_MAX
	size_t LOGIN_NAME_MAX = 256;
#endif
	char hostname[HOST_NAME_MAX];
	char username[LOGIN_NAME_MAX];
	gethostname(hostname, HOST_NAME_MAX);
	getlogin_r(username, LOGIN_NAME_MAX);

	// Get the start time
    auto timenow = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); 

	// and build the command to get the md5 checksum of myself
	char tmp[64];
	sprintf(tmp, "md5sum /proc/%d/exe", getpid());
	
	// parse the time
	runtime = convert_time(timestring);
	
	if(random_seed != 0) rng.seed(random_seed);
	
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Running Fleet on " << hostname << " with PID=" << getpid() << " by user " << username << " at " << ctime(&timenow);
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Executable checksum: " << system_exec(tmp);
	COUT "# \t --input=" << input_path ENDL;
	COUT "# \t --threads=" << nthreads ENDL;
	COUT "# \t --mcmc=" << mcmc_steps ENDL;
	COUT "# \t --mcts=" << mcts_steps ENDL;
	COUT "# \t --time=" << timestring << " (" << runtime << " seconds)" ENDL;
	COUT "# \t --restart=" << mcmc_restart ENDL;
	COUT "# \t --seed=" << random_seed ENDL;
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	
	
}
