#pragma once

#include <thread>
#include <mutex>
#include <iostream>
#include <fstream>
#include <string>

// Don't warn shadow on this its a nightmare
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "Dependencies/CL11.hpp"
#pragma GCC diagnostic pop

#include "Strings.h"

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

/**
 * @brief Simple saving of a vector of hypotheses
 * @param filename
 * @param hypotheses
 */
template<typename HYP>
void save(std::string filename, std::vector<HYP>& hypotheses) {
	
	std::ofstream out(filename);
	
	for(auto& v: hypotheses) {
		out << Q(v.string()) TAB v.parseable() ENDL;
	}
	
	out.close();	
}

/**
 * @brief Simple loading for a vector of hypotheses
 * @param filename
 * @param g
 * @return 
 */
template<typename HYP>
std::vector<HYP> load(std::string filename, typename HYP::Grammar_t* g) {
	
	std::vector<HYP> out;
	
	std::ifstream fs(filename);
	
	std::string line;
	while(std::getline(fs, line)) {
		auto parts = split(line, '\t');
		out.push_back(HYP::from_string(g,parts[1]));
//		CERR line ENDL;
	}
	return out;
}

// For loading data -- divides the columns and checks that there are N
// So we can use like:  for(auto [b1, b2, b3, b4, c] : read_csv<5>(FleetArgs::input_path)) {
template<size_t N>
std::vector<std::array<std::string,N>> read_csv(const std::string path, const char delimiter='\t', bool skipheader=true) {
	
	FILE* fp = fopen(path.c_str(), "r");
	if(fp==nullptr) { CERR "*** ERROR: Cannot open file " << path ENDL; exit(1);}
	
	char* line = nullptr; size_t len=0;
    
	if(skipheader) { auto __ignored = getline(&line, &len, fp); (void) __ignored; }
	
	std::vector<std::array<std::string,N>> out;	
	while( getline(&line, &len, fp) != -1 ) {
		if( line[0] == '#' ) continue;  // skip comments
		
		std::string s = line;
		
		// chomp the newline at the end
		if( (not s.empty()) and s[s.length()-1] == '\n') {
			s.erase(s.length()-1);
		}
		
		out.push_back(split<N>(s,delimiter));
	}
	fclose(fp);
	
	return out;
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

