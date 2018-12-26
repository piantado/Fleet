#pragma once

#include<map>
#include<vector>
#include<algorithm>

template<typename T>
class DiscreteDistribution {
	// This stores a distribution from values of T to log probabilities
	// it is used as the return value from calls with randomness
	
public:
	std::map<T,double> m; // map from values to log probabilities
	
	
	DiscreteDistribution() {};
	
	DiscreteDistribution(const DiscreteDistribution& d) : m(d.m) {
		
	}
	
	virtual ~DiscreteDistribution() {};
	
	virtual T argmax() const {
		T best{};
		double bestv = -infinity;
		for(auto a : m) {
			if(a.second > bestv) {
				bestv = a.second;
				best  = a.first;
			}
		}
		return best;
	}
	
	
	void print(std::ostream& out) {
		
		// put the strings into a vector
		std::vector<T> v;
		double Z = -infinity; // add up the mass
		for(auto a : m){ 
			v.push_back(a.first); 
			Z = logplusexp(Z, a.second);
		}
		
		// sort them to be increasing
		std::sort(v.begin(), v.end(), [this](T a, T b) { return this->m.at(a) > this->m.at(b); });
		
		
		out << "{";
		for(size_t i=0;i<v.size();i++){
			out << "'" << v[i] << "'" << ":" << m[v[i]];
			if(i < v.size()-1) { out << ", "; }
		}
		out << "} [Z=" << Z << ", N=" << v.size() << "]";
	}
	void print() {
		print(std::cout);
	}
		
	void addmass(T x, double v) {
		if(m.find(x) == m.end()) {
			m[x] = v;
		}
		else {
			m[x] = logplusexp(m[x], v);
		}
	}
	
	std::map<T,double>& values() {
		return m;
	}
	
	void operator<<(const DiscreteDistribution<T>& x) {
		// adds all of x to me
		for(auto a : x.values()) {
			addmass(a.first, a.second);
		}
	}
	
	std::vector<T> best(size_t N) {
		// get the N best
		std::vector<std::pair<T,double>> v(m.size());
		std::copy(m.begin(), m.end(), v.begin());
		std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.second > y.second; }); // put the big stuff first
		
		std::vector<T> out;
		for(size_t i=0;i<MIN(N, v.size());i++){
			out.push_back(v[i].first);
		}

		return out;
	}
	
	// inherit some interfaces
	size_t count(T x) const { return m.count(x); }
	size_t size() const     { return m.size(); }
	double operator[](T x)  { return m[x]; }
	double at(T x) const         { return m.at(x); }

	
};
