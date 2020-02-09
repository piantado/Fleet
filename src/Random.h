#pragma once

#include <random>
#include <functional>

#include "Numerics.h"

// it's important to make these thread-local or else they block each other in parallel cores
thread_local std::random_device rd;     // only used once to initialise (seed) engine
thread_local std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
thread_local std::uniform_real_distribution<double> uniform_dist(0,1.0);
thread_local std::normal_distribution<float> normal(0.0, 1.0);
	
double uniform() {
	/**
	 * @brief Sample from a uniform distribution
	 * @return 
	 */
	
	return uniform_dist(rng);
}

double cauchy_lpdf(double x, double loc=0.0, double gamma=1.0) {
	/**
	 * @brief Compute the log PDF of a cauchy distribution
	 * @param x - value
	 * @param loc - location
	 * @param gamma - scale
	 * @return 
	 */
	
    return -log(pi) - log(gamma) - log(1+((x-loc)/gamma)*((x-loc)/gamma));
}

double normal_lpdf(double x, double mu=0.0, double sd=1.0) {
	/**
	 * @brief Compute the log PDF of a normal distribution
	 * @param x
	 * @param mu
	 * @param sd
	 * @return 
	 */
	
    //https://stackoverflow.com/questions/10847007/using-the-gaussian-probability-density-function-in-c
    const float linv_sqrt_2pi = -0.5*log(2*pi*sd);
    return linv_sqrt_2pi  - ((x-mu)/sd)*((x-mu)/sd) / 2.0;
}

double random_cauchy() {
	/**
	 * @brief Generate a random sample from a standard cauchy
	 * @return 
	 */
	
	return tan(pi*(uniform()-0.5));
}

template<typename t>
std::vector<t> random_multinomial(t alpha, size_t len) {
	/**
	 * @brief Generate a random vector from a multinomial, with constant alpha
	 * @param alpha
	 * @param len
	 * @return 
	 */

	std::gamma_distribution<t> g(alpha, 1.0);
	std::vector<t> out(len);
	t total = 0.0;
	for(size_t i =0;i<len;i++){
		out[i] = g(rng);
		total += out[i];
	}
	for(size_t i=0;i<len;i++){
		out[i] /= total;
	}
	return out;	
}

template<typename T>
T myrandom(T max) {
	/**
	 * @brief My own random integer distribution, going [0,max-1]
	 * @param max
	 * @return 
	 */
	
	std::uniform_int_distribution<T> r(0,max-1);
	return r(rng);
}

template<typename T>
T myrandom(T min, T max) {
	/**
	 * @brief Random integer distribution [min,max-1]
	 * @param min
	 * @param max
	 * @return 
	 */
		
	std::uniform_int_distribution<T> r(min,max-1);
	return r(rng);
}


bool flip() {
	/**
	 * @brief Random bool
	 * @return 
	 */
	
	return uniform() < 0.5;
}


template<typename t, typename T> 
double sample_z(const T& s, std::function<double(const t&)>& f) {
	/**
	 * @brief If f specifies the probability (NOT log probability) of each element of s, compute the normalizing constant. 
	 * @param s - a collection of objects
	 * @param f - a function to map over s to get each probability
	 * @return 
	 */
		
	double z = 0.0;
	for(auto& x: s) {
		z += f(x);
	}
	return z; 
}

template<typename t, typename T> 
std::pair<t*,double> sample(const T& s, std::function<double(const t&)>& f = [](const t& v){return 1.0;}) {
	// this takes a collection T of elements t, and a function f
	// which assigns them each a probability, and samples from them according
	// to the probabilities in f. The probability that is returned is only the probability
	// of selecting that *element* (index), not the probability of selecting anything equal to it
	// (i.e. we defaultly don't double-count equal options). For that, use p_sample_eq below
	
	double z = sample_z(s,f);
	
	double r = z * uniform();
	
	for(auto& x : s) {
		double fx = f(x);
		r -= fx;
		if(r <= 0.0) 
			return std::make_pair(const_cast<t*>(&x), log(fx)-log(z));
	}
	
	assert(0 && "*** Should not get here in sampling");	
}

template<typename t, typename T> 
std::pair<t*,double> sample(const T& s, double(*f)(const t&)) {
	std::function sf = f;
	return sample<t,T>(s,sf);
}

template<typename t, typename T> 
double lp_sample_eq(const t& x, const T& s, std::function<double(const t&)>& f = [](const t& v){return 1.0;}) {
	// the probability of sampling *anything* equal to x out of s (including double counts)
	
	double z = sample_z(s,f);
	double px = 0.0; // find the probability of anything *equal* to x
	for(auto& y : s) { 
		if(y == x) 
			px += f(y); 
	}
	
	assert(px <= z);
	
	return log(px)-log(z);
}

template<typename t, typename T> 
double lp_sample_eq(const t& x, const T& s, double(*f)(const t&)) {
	std::function sf = f;
	return lp_sample_eq<t,T>(x,s,sf);
}

template<typename t, typename T> 
double lp_sample_one(const t& x, const T& s, std::function<double(const t&)>& f = [](const t& v){return 1.0;}) {
	// probability of sampling just this one
	
	double z = sample_z(s,f);
	return log(f(x))-log(z);
}
template<typename t, typename T> 
double lp_sample_one(const t& x, const T& s, double(*f)(const t&)) {
	std::function sf = f;
	return lp_sample_one<t,T>(x,s,sf);
}
