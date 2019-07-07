#pragma once

#include <mutex>

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

std::mutex output_lock;

/* Define some macros that make handling IO a little easier */

#define PRINT(x) { output_lock.lock(); std::cout << x; output_lock.unlock() }

#define PRINTN(x) { output_lock.lock(); std::cout << x << std::endl; output_lock.unlock() }


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


template<typename T>
void coutall(T x) {
	std::cout << x;
}
template<typename T, typename... Args>
void coutall(T x, Args... args) {
	coutall(x);
	std::cout << "\t";
	coutall(args...);
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