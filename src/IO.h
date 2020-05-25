#pragma once

#include <thread>
#include <mutex>
#include "dependencies/CL11.hpp"

//#include "fmt/format.h"

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

std::mutex output_lock;

/* Define some macros that make handling IO a little easier */

void __PRINT(){}
template<typename First, typename ...Rest>
void __PRINT(First && first, Rest && ...rest) {
    std::cout << std::forward<First>(first) << "\t";
    __PRINT(std::forward<Rest>(rest)...);
}
template<typename First, typename ...Rest>
void PRINT(First && first, Rest && ...rest) {
	std::lock_guard guard(output_lock);
	__PRINT(first,std::forward<Rest>(rest)...);
}
template<typename First, typename ...Rest>
void PRINTN(First && first, Rest && ...rest) {
	std::lock_guard guard(output_lock);
	__PRINT(first,std::forward<Rest>(rest)...);
	std::cout << std::endl;
}

template<typename First, typename ...Rest>
void DEBUG(First && first, Rest && ...rest) {
	std::lock_guard guard(output_lock);
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



