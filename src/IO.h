#pragma once

#include <thread>
#include <mutex>
#include "dependencies/CL11.hpp"

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

namespace Fleet {
	std::mutex output_lock;
}

/* Define some macros that make handling IO a little easier */

void __PRINT(){}
template<typename First, typename ...Rest>
void __PRINT(First && first, Rest && ...rest) {
    std::cout << std::forward<First>(first) << "\t";
    __PRINT(std::forward<Rest>(rest)...);
}
template<typename First, typename ...Rest>
void PRINT(First && first, Rest && ...rest) {
	std::lock_guard guard(Fleet::output_lock);
	__PRINT(first,std::forward<Rest>(rest)...);
}
template<typename First, typename ...Rest>
void PRINTN(First && first, Rest && ...rest) {
	std::lock_guard guard(Fleet::output_lock);
	__PRINT(first,std::forward<Rest>(rest)...);
	std::cout << std::endl;
}

template<typename First, typename ...Rest>
void DEBUG(First && first, Rest && ...rest) {
	std::lock_guard guard(Fleet::output_lock);
	std::cout << "DEBUG." << std::this_thread::get_id() << ": ";
	__PRINT(first,std::forward<Rest>(rest)...);
	std::cout << std::endl;
}

// ADD DEBUG(...) that is only there if we have a defined debug symbol....
// DEBUG(DEBUG_MCMC, ....)

//#define DEBUG(fmt, ...) 
// do { if (DEBUG) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
				
//class DebugBlock {
//	// Handy debug printing when a variable enters and exists scope
//	std::string enter;
//	std::string exit; 
//	public:
//	DebugBlock(std::string en, std::string ex) : enter(en), exit(ex) {
//		COUT enter ENDL;
//	}
//	~DebugBlock() {
//		COUT exit ENDL;
//	}
//};


//template<typename T>
//void _print_wrapper(T x, std::ostream& o) {
//	o << x;
//}
//template<typename T, typename... Args>
//void _print_wrapper(T x, Args... args, std::ostream& o, std::string sep) {
//	_print_wrapper(x,o);
//	o << sep;
//	_print_wrapper(args..., o, sep);
//}
//
//
//template<typename... Args>
//void PRINT(Args... args, std::string sep='\t', std::string end='\n') {
//	// this appends end to the args
//	std::lock_guard guard(Fleet::output_lock);
//	_print_wrapper(args..., end, sep, std::cout);
//}

// Want: A handy debugging macro that takes a variable name and prints 
// variadic tab separated list of args
// Bleh can't seem to do this bc https://stackoverflow.com/questions/2831934/how-to-use-if-inside-define-in-the-c-preprocessor
/*
 #define MYDEBUG(name, ...) \
	#ifdef name \
		coutall(__VA_ARGS__);\
		std::cout std::endl; \
	#endif
*/


//template<typename a, typename... ARGS>
//void PRINT()

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
bool          quiet      = false; // this is used to indicate that we want to not print much out (typically only posteriors and counts)
std::string   input_path   = "input.txt";
std::string   tree_path    = "tree.txt";
std::string   output_path  = "output";
std::string   timestring   = "0s";

namespace Fleet { 	
	CLI::App DefaultArguments(const char* brief) {
		CLI::App app{brief};
		
		app.add_option("-R,--seed",    random_seed, "Seed the rng (0 is no seed)");
		app.add_option("-s,--mcts",    mcts_steps, "Number of MCTS search steps to run");
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
		
		app.add_flag(  "-q,--quiet",  quiet, "Don't print very much and do so on one line");
//		app.add_flag(  "-C,--checkpoint",   checkpoint, "Checkpoint every this many steps");

		return app; 
	}
}

