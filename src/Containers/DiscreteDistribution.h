#pragma once

#include<map>
#include<vector>
#include<algorithm>
#include <iostream>
#include <assert.h>
#include <memory>

#include "Miscellaneous.h"
#include "Numerics.h"
#include "IO.h"
#include "Strings.h"



/**
 * @class DiscreteDistribution
 * @author steven piantadosi
 * @date 03/02/20
 * @file DiscreteDistribution.h
 * @brief This stores a distribution from values of T to log probabilities. It is used as the return value from calls with randomness
 */
template<typename T>
class DiscreteDistribution {
	
public:

	// m here will store the map from values to log probabilities. We need to ensure, though
	// that when T is either float or double, it correctly sorts NaN (which std::less does not defaulty do)
	// so, here we have a conditional to handle that, what a nightmare
	using my_map_t = typename std::conditional< std::is_same<T,double>::value or std::is_same<T,float>::value,
								   	            std::map<T,double,floating_point_compare<double>>,
									            std::map<T,double>>::type;
	my_map_t m;

	DiscreteDistribution() { }
	
	virtual T argmax() const {
		T best{}; // Note defaults to this when there are none!
		double bestv = -infinity;
		for(auto a : m) {
			if(a.second > bestv) {
				bestv = a.second;
				best  = a.first;
			}
		}
		return best;
	}
	
	
	void show(std::ostream& out, unsigned long nprint=0) const { out << this->string(nprint);	}
	void show(unsigned long nprint=0)                  const {	show(std::cout, nprint);	}
	
	void erase(const T& k) {
		m.erase(k);
	}
	
	
	std::string string(unsigned long nprint=0) const {
		/**
		 * @brief Convert this distribution into a string, printing at most nprint
		 * @param nprint
		 * @return 
		 */
		
		// put the strings into a vector
		std::vector<T> v;
		double z = Z();
		
		for(const auto& a : m) {
			v.push_back(a.first);
		}
		
		// sort them to be increasing
		std::sort(v.begin(), v.end(), [this](T a, T b) { return this->m.at(a) > this->m.at(b); });
		
		
		std::string out = "{";
		auto upper_bound = (nprint == 0 ? v.size() : std::min(v.size(), nprint));
		for(size_t i=0;i<upper_bound;i++){
			out += "'"  + str(v[i]) + "':" + std::to_string(m.at(v[i]));
			if(i < upper_bound-1) { out += ", "; }
		}
		out += "} [Z=" + str(z) + ", N=" + std::to_string(v.size()) + "]";
		
		return out;
	}
	
		
	void addmass(T x, double v) {
		/**
		 * @brief Add log probability v to type x
		 * @param x
		 * @param v
		 */
		
		// We can't store NaN in this container ugh
		if constexpr (std::is_same<T, double>::value or 
					  std::is_same<T, float>::value){
			assert((not std::isnan(x)) && "*** Cannot store NaNs here, sorry. They don't work with maps and you'll be in for an unholy nightmare");
		}
		
		if(m.find(x) == m.end()) {
			m[x] = v;
		}
		else {
			m[x] = logplusexp(m[x], v);
		}
	}
	
	double get(T x, double v) const {
		if(m.find(x) == m.end()) {
			return v;
		} 
		else {
			return m.at(x);
		}
	}
	
	bool contains(const T& x) const { 
		return m.contains(x);
	}
	
	auto begin() { return m.begin(); }
	auto end()   { return m.end();   }
	
	const std::map<T,double>& values() const {
		/**
		 * @brief Get all of the values in this distribution
		 * @return 
		 */		
		return m;
	}
	
	void operator<<(const DiscreteDistribution<T>& x) {
		// adds all of x to me
		for(auto a : x.values()) {
			addmass(a.first, a.second);
		}
	}
	
	double Z() const {
		double Z = -infinity; // add up the mass
		for(const auto& a : m){ 
			Z = logplusexp(Z, a.second);
		}
		return Z;
	}
	
	double lp(const T& x) {
		/**
		 * @brief Retun the log probability of x, including the normalizing term (NOTE: This makes this O(N) to compute theo normalizer. So this is bad to use if you have to iterate over the set -- you shoul call Z() separately then
		 * @param N
		 * @return 
		 */
		if(m.count(x)) {
			return m[x]-Z();
		}
		else {
			return -infinity;
		}
	}
	
	
	std::vector<T> best(size_t n, bool include_equal) const {
		/**
		 * @brief Get the N best from this distribution
		 * @param N - how many to get
		 * @param include_equal - should we include ones that are equal to the best (potentially giving more than N)?
		 * @return 
		 */
		
		std::vector<std::pair<T,double>> v(m.size());
		std::copy(m.begin(), m.end(), v.begin());
		std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.second > y.second; }); // put the big stuff first
		
		std::vector<T> out;
		auto until = n-1;
		for(size_t i=0; (i<v.size()) and ((i<=until) or (include_equal and v[i].second == v[until].second));i++){
			out.push_back(v[i].first);
		}

		return out;
	}
	
	std::vector<std::pair<T,double>> sorted(bool decreasing=false) const {
		/**
		 * @brief Get this distribution as a sorted vector of pairs
		 * @param decreasing
		 */
		std::vector<std::pair<T,double>> v(m.size());
		std::copy(m.begin(), m.end(), v.begin());
		if(decreasing)	std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.second > y.second; }); // put the big stuff first
		else            std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.second < y.second; }); // put the big stuff first
		return v;		
	}
	
	
	// inherit some interfaces
	size_t count(T x) const { return m.count(x); }
	size_t size() const     { return m.size(); }
	double operator[](const T x)  {
		if(m.count(x)) return m[x];
		else		   return -infinity;
	}
//	double& operator[](const T x)  {
//		return m[x];
//	}
	double at(T x) const         { 
		if(m.count(x)) return m.at(x);
		else		   return -infinity;
	}


	/**
	 * @brief This compares using just a standard ordering on keys -- mainly here so we can
	 * 		  put DiscreteDistributions in maps
	 * @param x
	 * @return 
	 */
	auto operator<=>(const DiscreteDistribution<T>& x) const {
		return m <=> x.m;
	}
	
	bool operator==(const DiscreteDistribution<T>& other) const {
		// we can compare elements -- NOTE we set a tolerance so they don't have to be exactly
		// equal (I hope this doesn't cause you troubles)
		const double threshold = 1e-6;
		
		std::set<T> keys;
		std::transform(m.begin(),             m.end(), std::inserter(keys, keys.end()), [](auto pair){ return pair.first; });
		std::transform(other.m.begin(), other.m.end(), std::inserter(keys, keys.end()), [](auto pair){ return pair.first; });
		
		for(auto& k : keys) {
			if(abs(at(k) - other.at(k)) > threshold) 
				return false; 
		}
		
		return true; 
	}
	
};


template<typename T>
std::ostream& operator<<(std::ostream& o, const DiscreteDistribution<T>& x) {
	o << x.string();
	return o;
}

template<typename T>
std::string str(const DiscreteDistribution<T>& a ){
	return a.string();
}
