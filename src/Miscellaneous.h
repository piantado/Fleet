#pragma once

#include <memory>
#include <chrono>

#include "Numerics.h"

//#include "IO.h"

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

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// inverse logit is pretty usefule
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
T inv_logit(const T z) {
	return 1.0 / (1.0 + exp(-z));
}


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Python-like pass statements
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define pass ((void)0)

template <typename T>
void UNUSED(const T& x) {}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Hash combinations
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Hash combination from https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
inline void hash_combine(std::size_t& seed) { }

/**
 * @brief Combine hash functions
 * @param seed
 * @param v
 */
template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
	
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    hash_combine(seed, rest...);
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Default getter for map
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/**
 * @brief A getter for std::map that lets us specify a default to use when the key is missing
 * @param m
 * @param key
 * @param def
 * @return 
 */
template<typename M>
typename M::mapped_type get(const M& m, const typename M::key_type& key, const typename M::mapped_type& def) {
	if(m.contains(key)) return m.at(key);
	else                return def;
}


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
/// Find the index of a type in variadic args
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/// Helpers to Find the numerical index (as a nonterminal_t) in a tuple of a given type
// from https://stackoverflow.com/questions/42258608/c-constexpr-values-for-types
template <class T, class Tuple> 
struct TypeIndex;

template <class T, class... Types>
struct TypeIndex<T, std::tuple<T, Types...>> {
	static const size_t value = 0;
};

template <class T, class U, class... Types>
struct TypeIndex<T, std::tuple<U, Types...>> {
	static const size_t value = 1 + TypeIndex<T, std::tuple<Types...>>::value;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Check if variadic args contain a type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

///**
// * @brief Check if a type is contained in parameter pack
// * @return 
// */
template<typename X, typename... Ts>
constexpr bool contains_type() {
	return std::disjunction<std::is_same<X, Ts>...>::value;
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Check if a type is a specialization of another
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


//https://stackoverflow.com/questions/16337610/how-to-know-if-a-type-is-a-specialization-of-stdvector
template<typename Test, template<typename...> class Ref>
struct is_specialization : std::false_type {};

template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref>: std::true_type {};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Weighted quantiles
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
T weighted_quantile(std::vector<std::pair<T,double>>& v, double q) {
	// find the weighted quantile, from a list of pairs <T,logprobability>
	// NOTE: may modify v..
	
	std::sort(v.begin(), v.end()); // sort by T 
	
	// get the normalizer (log space)
	double z = -infinity;
	for(auto [x,lp] : v) z = logplusexp(z,lp);
	
	double cumulative = -infinity;
	for(auto [x,lp] : v) {
		cumulative = logplusexp(cumulative, lp);
		if(exp(cumulative-z) >= q) {
			return x;
		}
	}
	
	assert(false);
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Mean, sd
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
double mean(std::vector<T>& v){
	double s = 0.0;
	for(auto& x : v) s += x;
	return s/v.size();
}

template<typename T>
double sd(std::vector<T>& v) {
	assert(v.size() > 1);
	double m = mean(v);
	double s = 0.0;
	for(auto& x : v) {
		s += pow(m-x,2.);
	}
	return sqrt(s / (v.size()-1));
}

template<typename T>
double median(std::vector<T>& v) {
	const size_t n = v.size();
	assert(n> 0);
	std::sort(v.begin(), v.end());
	
	if(n % 2 == 0) {
		return (v[n/2] + v[n/2-1]) / 2;
	}
	else {
		return v[n/2];
	}
}

/**
 * @brief This allows sorting of things that contain NaN
 * @param x
 * @param y
 * @return 
 */
template<typename T>
struct floating_point_compare {
	bool operator()(const T &x, const T &y) const {
		if(std::isnan(x)) return false;
		if(std::isnan(y)) return true;
		return x < y;
	}
};