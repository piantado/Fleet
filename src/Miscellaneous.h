#pragma once

#include <chrono>
#include <vector>

#include "IO.h"

typedef struct t_null {} t_null; // just a blank type


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

/* If x is a prefix of y -- works for strings and vectors */
template<typename T>
bool is_prefix(const T& prefix, const T& x) {
	if(prefix.size() > x.size()) return false;
	if(prefix.size() == 0) return true;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Just a convenient wrapper for timing
/// this times between successive calls to tic()
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

auto ticstart = std::chrono::high_resolution_clock::now();
std::chrono::duration<double> ticelapsed;

auto now() { 
	return std::chrono::high_resolution_clock::now();
}
double time_since(std::chrono::time_point<std::chrono::high_resolution_clock> x) {
	auto n = now();
	return std::chrono::duration_cast<std::chrono::duration<double>>(n-x).count();
}

void tic() {
	// record the amount of time since the last tic()
	auto x = now();
	ticelapsed =  std::chrono::duration_cast<std::chrono::duration<double>>(x-ticstart);
	ticstart = x;
}

double elapsed_seconds() {
	return ticelapsed.count(); 
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
		
	return (unsigned long)(t*multiplier); // note this effectively rounds to the nearest second 
	
}

// Python-like pass statements
#define pass ((void)0)


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Template magic to check if a class is iterable
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//https://stackoverflow.com/questions/13830158/check-if-a-variable-is-iterable
template <typename T, typename = void>
struct is_iterable : std::false_type {};

// this gets used only when we can call std::begin() and std::end() on that type
template <typename T>
struct is_iterable<T, std::void_t<decltype(std::begin(std::declval<T>())),
                                  decltype(std::end(std::declval<T>()))
                                 >
                  > : std::true_type {};

// Here is a helper:
template <typename T>
constexpr bool is_iterable_v = is_iterable<T>::value;