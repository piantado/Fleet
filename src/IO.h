#pragma once

#include <mutex>

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

namespace Fleet {
	std::mutex output_lock;
}

/* Define some macros that make handling IO a little easier */

//#define PRINT(x) { output_lock.lock(); std::cout << x; output_lock.unlock() }
//
//#define PRINTN(x) { output_lock.lock(); std::cout << x << std::endl; output_lock.unlock() }


class DebugBlock {
	// Handy debug printing when a variable enters and exists scope
	std::string enter;
	std::string exit; 
	public:
	DebugBlock(std::string en, std::string ex) : enter(en), exit(ex) {
		COUT enter ENDL;
	}
	~DebugBlock() {
		COUT exit ENDL;
	}
};


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


std::string QQ(std::string x) {
	return std::string("\"") + x + std::string("\"");
}
std::string Q(std::string x) {
	return std::string("\'") + x + std::string("\'");
}


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

