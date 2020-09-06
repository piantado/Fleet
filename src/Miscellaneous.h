#pragma once

#include <chrono>
#include <vector>

#include "IO.h"

// This disables unusued parameter warnings. 
#define UNUSED(x) (void)(x) 

typedef struct null_t {} null_t; // just a blank type

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start, int len) {
	/**
	 * @brief Take a slice of a vector v starting at start of length len
	 * @param v
	 * @param start
	 * @param len
	 * @return 
	 */
	
	std::vector<T> out;
	if(len > 0) {
		out.resize(len); // in case len=-1
		std::copy(v.begin() + start, v.begin()+start+len, out.begin());
	}
	return out;
}

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start) {
	/**
	 * @brief Take a slice of a vector until its end
	 * @param v
	 * @param start
	 * @return 
	 */
		
	// just chop off the end 
	return slice(v, start, v.size()-start);
}

template<typename T>
void increment(std::vector<T>& v, size_t idx, T count=1) {
	// Increment and potentially increase my vector size if needed
	if(idx > v.size()) 
		v.resize(idx+1,0);
	v[idx] += count;
}


/* If x is a prefix of y -- works for strings and vectors */
template<typename T>
bool is_prefix(const T& prefix, const T& x) {
	/**
	 * @brief For any number of iterable types, is prefix a prefix of x
	 * @param prefix
	 * @param x
	 * @return 
	 */
		
	if(prefix.size() > x.size()) return false;
	if(prefix.size() == 0) return true;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}


// From https://stackoverflow.com/questions/478898/how-to-execute-a-command-and-get-output-of-command-within-c-using-posix
std::string system_exec(const char* cmd) {
	/**
	 * @brief Call cmd on the system
	 * @param cmd
	 * @return 
	 */
		
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


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// A reserved size vector type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T, size_t N>
class ReservedVector : public std::vector<T> {
public:
	ReservedVector() { 
		this->reserve(N);
	}
};

