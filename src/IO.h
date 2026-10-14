#pragma once

#include <thread>
#include <mutex>
#include <iostream>
#include <fstream>
#include <string>

#include "OrderedLock.h"
#include "Strings.h"

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
	
	if(fs.fail()) { 
		assert(false && "*** Error file could not be opened.");
	}
	
	std::string line;
	while(std::getline(fs, line)) {
		if(line.length() > 0 and line[0] != '#'){
			ret += line + "\n";
		}
	}
	if(ret.size() > 0) 
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
	
	std::ifstream file(path);
	if(file.fail()){ print("*** File does not exist", path); assert(false); }

	std::string s;
	std::vector<std::array<std::string,N>> out;	
	while( std::getline(file,s) ) {
		if(s.size() == 0 or s[0] == '#') continue; // skip comments and blanks
				
		if(skipheader) { // skipping the header
			skipheader = false;
			continue;
		}
		
		out.push_back(split<N>(s,delimiter));
	}
	return out;
}


std::vector<std::vector<std::string>> read_csv(const std::string path, bool skipheader, const char delimiter='\t') {
	
	std::ifstream file(path);
	if(file.fail()){ print("*** File does not exist", path); assert(false); }

	std::string s;
	std::vector<std::vector<std::string>> out;	
	while( std::getline(file,s) ) {
		if(s.size() == 0 or s[0] == '#') continue; // skip comments and blanks
				
		if(skipheader) { // skipping the header
			skipheader = false;
			continue;
		}
		
		auto q = split(s,delimiter);
		std::vector<std::string> v;
		while(! q.empty()) {
			v.push_back(q.front()); q.pop_front();			
		}
		out.push_back(v);
	}
	return out;
}

