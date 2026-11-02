#pragma once

#include <thread>
#include <mutex>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

#include "OrderedLock.h"
#include "Strings.h"
#include "Serialization.h"

/**
 * @brief Simple saving of a vector (or any iterable) of hypotheses, one serialization per line.
 *        Throws if the file cannot be written, or if some serialization contains a newline (which
 *        load could not read back -- e.g. a TopN; use save(filename, TopN) / load_topn for those).
 *        Everything is serialized before the file is opened, so a serialization failure leaves no partial file.
 * @param filename
 * @param hypotheses
 */
template<typename T>
void save(std::string filename, T& hypotheses) {
	
	std::vector<std::string> lines;
	for(auto& v: hypotheses) {
		lines.push_back(v.serialize());
		if(contains(lines.back(), '\n')) {
			throw std::runtime_error("*** save: element " + str(lines.size()-1) + " serializes to several lines, so load could not read it back: " + QQ(lines.back()));
		}
	}
	
	std::ofstream out(filename);
	if(not out) { throw std::runtime_error("*** save: could not open " + filename + " for writing"); }
	for(auto& l : lines) {
		out << l << "\n";
	}
	out.close();	
	if(out.fail()) { throw std::runtime_error("*** save: error while writing " + filename); }
}

/**
 * @brief Simple loading for a vector of hypotheses, one per line (see save). Blank lines, lines
 *        starting with '#', and a trailing '\r' (Windows line endings) are skipped.
 *        Throws std::runtime_error if the file cannot be opened, and DeserializationError (with
 *        the file name and line number) on a line that cannot be deserialized.
 * @param filename
 * @return 
 */
template<typename HYP>
std::vector<HYP> load(std::string filename) {
	
	std::vector<HYP> out;
	
	std::ifstream fs(filename);
	if(not fs) { throw std::runtime_error("*** load: could not open " + filename); }
	
	std::string line;
	size_t lineno = 0;
	while(std::getline(fs, line)) {
		lineno++;
		if(line.size() > 0 and line.back() == '\r') line.pop_back();
		if(line.empty() or line[0] == '#') continue;
		try {
			out.push_back(HYP::deserialize(line));
		} catch(DeserializationError& e) {
			throw DeserializationError("*** load: " + filename + " line " + str(lineno) + ": " + e.what());
		}
	}
	if(fs.bad()) { throw std::runtime_error("*** load: error while reading " + filename); }
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

