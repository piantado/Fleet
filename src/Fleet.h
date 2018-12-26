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
#include <stack>
#include <vector>
#include <queue>
#include <tuple>
#include <map>
#include <atomic>
#include<iostream>
#include <assert.h>
#include <string.h>
#include <memory>
#include <pthread.h>
#include <chrono>
#include <thread>         // std::this_thread::sleep_for

#include <sys/resource.h> // just for setting priority defaulty 

const std::string FLEET_VERSION = "0.0.2";

// First some error checking on Fleet's required macros
// this is because we use some of them in enums, and so a failure 
// to define them might cause an error that is silent or hard 
// to detect. 
#ifndef NT_NAMES
#error You must define NT_NAMES (the names of nonterminals)
#endif

#ifndef NT_TYPES
#error You must define a list NT_TYPES of types in the evaluation stack, or correspondingly nonterminals
#endif

// TODO: Fix enum scoping for t_abort and t_nonterminal

enum abort_t {NO_ABORT=0, RECURSION_DEPTH, RANDOM_CHOICE, SIZE_EXCEPTION, OP_ERR_ABORT, RANDOM_BREAKOUT}; // setting NO_ABORT=0 allows us to say if(aborted)...

// Define our opertions as an enum. NOTE: They can NOT be strongly typed (enum class) because
// we store ints in an array of the same type.
// These defined ones are processed in a VirtualMachineState, the rest are defined in a function dispatch_rule
//enum op_t { op_NOP,op_X,op_POPX,op_MEM,op_RECURSE,op_RECURSE_FACTORIZED,op_MEM_RECURSE,op_MEM_RECURSE_FACTORIZED,op_FLIP,op_FLIPP,op_IF,op_JMP,
//			MY_OPS, // MY_OPS come at the end so that I can include additional, non-declared ones if I want to by int index
//			};

enum nonterminal_t {NT_NAMES, N_NTs };


// convenient to make op_NOP=0, so that the default initialization is a NOP
enum BuiltinOp {
	op_NOP=0,op_X,op_POPX,op_MEM,op_RECURSE,op_RECURSE_FACTORIZED,op_MEM_RECURSE,op_MEM_RECURSE_FACTORIZED,op_FLIP,op_FLIPP,op_IF,op_JMP
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

unsigned long mcts_steps   = 100; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long mcmc_steps   = 100000; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long thin         = 0;
unsigned long ntop         = 100;
unsigned long mcmc_restart = 0;
unsigned long checkpoint   = 0; 
double        explore      = 1.0; // we want to exploit the string prefixes we find
size_t        nthreads     = 1;
long          runtime      = 0;
unsigned long chains       = 1;
bool          concise      = false; // this is used to indicate that we want to not print much out (typically only posteriors and counts)
std::string   input_path   = "input.txt";
std::string   tree_path    = "tree.txt";
std::string   output_path  = "output.txt";

#define FLEET_DECLARE_GLOBAL_ARGS() \
    CLI::App app{"Fancier number model."};\
    app.add_option("-s,--steps",    mcts_steps, "Number of MCTS search steps to run");\
    app.add_option("-m,--mcmc",     mcmc_steps, "Number of mcmc steps to run");\
    app.add_option("-t,--thin",     thin, "Thinning on the number printed");\
    app.add_option("-o,--output",   output_path, "Where we write output");\
	app.add_option("-O,--top",      ntop, "The number to store");\
	app.add_option("-n,--threads",  nthreads, "Number of threads for parallel search");\
    app.add_option("-e,--explore",  explore, "Exploration parameter for MCTS");\
    app.add_option("-r,--restart",  mcmc_restart, "If we don't improve after this many, restart");\
    app.add_option("-i,--input",    input_path, "Read standard input from here");\
	app.add_option("-T,--time",     runtime, "Stop (via CTRL-C) after this much time");\
	app.add_option("-E,--tree",     tree_path, "Write the tree here");\
	app.add_flag(  "-q,--concise",  concise, "Don't print very much and do so on one line");\
	app.add_flag(  "-c,--chains",   chains, "How many chains to run");\
	app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");\
	

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef std::stack<Instruction> Program;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We defaultly include all of the major requirements for Fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Interfaces.h"
#include "Miscellaneous.h"
#include "Rule.h"
#include "VirtualMachinePool.h"
#include "VirtualMachineState.h"
#include "Node.h"
//#include "Node.cpp"
#include "Grammar.h"
#include "CaseMacros.h"
#include "DiscreteDistribution.h"

#include "LOTHypothesis.h"
#include "Lexicon.h"

#include "Inference/MCMC.h"
#include "Inference/MCTS.h"

#include "Top.h"
#include "CL11.hpp"

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

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

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
	
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Running Fleet on " << hostname << " with PID=" << getpid() << " by user " << username << " at " << ctime(&timenow);
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	COUT "# Executable checksum: " << system_exec(tmp);
	COUT "# \t --input=" << input_path ENDL;
	COUT "# \t --nthreads=" << nthreads ENDL;
	COUT "# \t --mcmc=" << mcmc_steps ENDL;
	COUT "# \t --time=" << runtime ENDL;
	COUT "# \t --restart=" << mcmc_restart ENDL;
	COUT "# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" ENDL;
	
	
}

