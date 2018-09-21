/* Globally shared variables and declarations */

#ifndef FLEET_H
#define FLEET_H

#include "stdlib.h"
#include "stdio.h"
#include "math.h"
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <stack>
#include <vector>
#include <queue>
#include <tuple>
#include <map>
#include<iostream>
#include <assert.h>
#include <string.h>
#include <memory>
#include <pthread.h>
#include <chrono>

const std::string FLEET_VERSION = "0.0.1";

// First some error checking on Fleet's required macros
// this is because we use some of them in enums, and so a failure 
// to define them might cause an error that is silent or hard 
// to detect. NOTE: We must include bool in order to define op_Flip
#ifndef NT_NAMES
#error You must define NT_NAMES (the names of nonterminals)
#endif


// NOTE: short is reserved for indexing into factorized hypotheses
#ifndef NT_TYPES
#error You must define a lit NT_TYPES of types in the evaluation stack, or correspondingly nonterminals
#endif

#ifndef MY_OPS
#error You must define MY_OPS as a list of operation names that your primitives use
#endif

// TODO: Fix enum scoping for t_abort and t_nonterminal

enum t_abort {NO_ABORT=0, RECURSION_DEPTH, RANDOM_CHOICE, SIZE_EXCEPTION, OP_ERR_ABORT, RANDOM_BREAKOUT}; // setting NO_ABORT=0 allows us to say if(aborted)...

// Define our opertions as an enum. NOTE: They can NOT be strongly typed (enum class) because
// we store ints in an array of the same type.
// These defined ones are processed in a VirtualMachineState, the rest are defined in a function dispatch_rule
// op_ERR here is used to throw errors by push_program
enum op_t { MY_OPS,
			op_NOP,op_X,op_POPX,op_RECURSE,op_RECURSE_FACTORIZED,op_FLIP,op_IF,op_JMP,
			N_OPs};

enum t_nonterminal {NT_NAMES, N_NTs };


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// A Handler for CTRL_C
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// NOTE: this must be registered in main with signal(SIGINT, fleet_interrupt_handler);
#include <signal.h>
volatile sig_atomic_t CTRL_C = false;
void fleet_interrupt_handler(int signum) { CTRL_C = true; } 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These are standard variables that occur nearly universally in these searches
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

unsigned long mcts_steps   = 100; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long mcmc_steps   = 10000; // note this also controls how quickly/deep the search goes into the lexicon
unsigned long thin         = 0;
unsigned long ntop         = 100;
unsigned long mcmc_restart = 0;
double        explore      = 1.0; // we want to exploit the string prefixes we find
size_t        nthreads     = 1;
unsigned long global_sample_count = 0;
std::string   input_path = "input.txt";
std::string   tree_path  = "tree.txt";


#define FLEET_HANDLE_GLOBAL_ARGS() \
    CLI::App app{"Fancier number model."};\
    app.add_option("-s,--steps",    mcts_steps, "Number of MCTS search steps to run");\
    app.add_option("-m,--mcmc",     mcmc_steps, "Number of mcmc steps to run");\
    app.add_option("-t,--thin",     thin, "Thinning on the number printed");\
    app.add_option("-O,--top",      ntop, "The number to store");\
	app.add_option("-n,--threads",  nthreads, "Number of threads for parallel search");\
    app.add_option("-e,--explore",  explore, "Exploration parameter for MCTS");\
    app.add_option("-r,--restart",  mcmc_restart, "If we don't improve after this many, restart");\
    app.add_option("-i,--input",    input_path, "Read standard input from here");\
	app.add_option("-T,--tree",     tree_path, "Write the tree here");\
	CLI11_PARSE(app, argc, argv);\


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is how programs are represented
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef std::stack<op_t> Opstack;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We defaultly include all of the major requirements for Fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Interfaces.h"
#include "Miscellaneous.h"
#include "Program.h"
#include "Rule.h"
#include "VirtualMachinePool.h"
#include "VirtualMachineState.h"
#include "Node.h"
#include "Grammar.h"
#include "CaseMacros.h"
#include "IO.h"
#include "Distribution.h"

#include "LOTHypothesis.h"
#include "Lexicon.h"

#include "Inference/MCMC.h"
#include "Inference/MCTS.h"
#include "Inference/Playouts.h"

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


#endif