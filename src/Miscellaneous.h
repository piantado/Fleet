#pragma once

#include <chrono>
#include <vector>

#include "IO.h"

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start, int len) {
	// slice of a vector
	std::vector<T> out;
	if(len > 0) {
		out.resize(len); // in case len=-1
		std::copy(v.begin() + start, v.begin()+start+len, out.begin());
	}
	return out;
}

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start) {
	// just chop off the end 
	return slice(v, start, v.size()-start);
}


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


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Time conversions for fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

unsigned long convert_time(std::string& s) {
	// Converts our own time format to seconds, which is what Fleet's time utilities use
	// the time format we accept is #+(.#+)[smhd] where shmd specifies seconds, minutes, hours days 
	
	// specila case of s="0" will be allowed
	if(s == "0") return 0;
		
	// else we must specify a unit	
	double multiplier; // for default multiplier of 1 is seconds
	switch(s.at(s.length()-1)) {
		case 's': multiplier = 1; break; 
		case 'm': multiplier = 60; break;
		case 'h': multiplier = 60*60; break;
		case 'd': multiplier = 60*60*24; break;
		default: 
			CERR "*** Unknown time specifier: " << s.at(s.length()-1) << " in " << s << ". Did you forget a unit?" ENDL;
			assert(0);
	}
	
	double t = std::stod(s.substr(0,s.length()-1)); // all but the last character
		
	return (unsigned long)(t*multiplier); // note this effectively rounds to the nearest escond 
	
}

