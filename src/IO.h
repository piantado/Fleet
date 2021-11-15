#pragma once

#include <thread>
#include <mutex>
#include <iostream>
#include <fstream>
#include <string>

#include "OrderedLock.h"
#include "Strings.h"

// Don't warn shadow on this its a nightmare
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "Dependencies/CL11.hpp"
#pragma GCC diagnostic pop

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define NL <<"\n"<<
#define ENDL <<std::endl;
#define ENDLL <<"\n"<<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<
#define COUTT std::cout<<"\t"<<
#define COUTTT std::cout<<"\t\t"<<

const std::string OUTPUT_SEP = "\t";  // standard output separator

// These locks manage the standard output in PRINTN and ERRN below
OrderedLock output_lock;
OrderedLock err_lock;

/**
 * @brief This assumes o has been appropriately locked
 * @param o
 * @param f
 */
template<typename FIRST, typename... ARGS>
void OUTPUTN(std::ostream& o, FIRST f, ARGS... args) {
	o << f; 
	if constexpr(sizeof...(ARGS) > 0) {
		((o << OUTPUT_SEP << args), ...);	
	}
	o << std::endl;
}

/**
 * @brief Lock output_lcok and print to std:cout
 * @param f
 */
template<typename FIRST, typename... ARGS>
void PRINTN(FIRST f, ARGS... args) {
	std::lock_guard guard(output_lock);
	OUTPUTN(std::cout, f, args...);
}

/**
 * @brief Lock err_lock and print to std:cerr
 * @param f
 */
template<typename FIRST, typename... ARGS>
void ERRN(FIRST f, ARGS... args) {
	std::lock_guard guard(err_lock);
	OUTPUTN(std::cerr, f, args...);
}

/**
 * @brief Print to std:ccout with debugging info
 * @param f
 */
template<typename FIRST, typename... ARGS>
void DEBUG(FIRST f, ARGS... args) {
	std::lock_guard guard(output_lock);
	OUTPUTN(std::cout, "DEBUG", std::this_thread::get_id(), f, args...);
}

/**
 * @brief Simple saving of a vector of hypotheses
 * @param filename
 * @param hypotheses
 */
template<typename T>
void save(std::string filename, T& hypotheses) {
	
	std::ofstream out(filename);
	
	for(auto& v: hypotheses) {
		out << v.serialize() ENDL;
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
std::vector<HYP> load(std::string filename) {
	
	std::vector<HYP> out;
	
	std::ifstream fs(filename);
	
	std::string line;
	while(std::getline(fs, line)) {
//		auto [col1, col2] = split<2>(line, '\t');
		out.push_back(HYP::deserialize(line)); // we've stored hypotheses in position 1
//		CERR line ENDL;
	}
	return out;
}

/**
 * @brief Read this entire file as a string
 *        Skipping comment # lines
 * @param filename
 * @return 
 */
std::string gulp(std::string filename) {
	std::string ret;
	std::ifstream fs(filename);
	
	std::string line;
	while(std::getline(fs, line)) {
		if(line.length() > 0 and line[0] != '#'){
			ret += line + "\n";
		}
	}
	ret.erase(ret.size()-1); // remove last newline
	return ret;
}

/**
 * @brief Load data -- divides in columns at the delimiter. NOTE: we are required to say whether we skip
 *        a header (first line) or not. Checks tha tthere are exactly N on each line. 
 *        So we can use like:  
 * 				for(auto [b1, b2, b3, b4, c] : read_csv<5>(FleetArgs::input_path, true)) { 
 * 					....
 * 				}
 * @param filename
 * @param g
 * @return 
 */
template<size_t N>
std::vector<std::array<std::string,N>> read_csv(const std::string path, bool skipheader, const char delimiter='\t') {
	
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
		
		if( s.length() == 0) continue; // skip empty lines 
		assert(s.length() >= N-1 && "*** Error with reading line -- not enough delimiters");
				
		out.push_back(split<N>(s,delimiter));
	}
	fclose(fp);
	
	return out;
}
