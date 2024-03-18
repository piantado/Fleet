
#pragma once

#include <string>

#include "generator.hpp"

// We'll use cppcoro's generator for now, where is std::generator
template<typename T>
using generator = cppcoro::generator<T>;

// thin samples by N
struct thin {
	size_t m;
	size_t cnt;
	
	thin(size_t _m) : m(_m), cnt(0) { 
	}

	// funny little increment here returns true for 0 mod m
	bool operator++() {
		if(++cnt == m or m==0) {
			cnt = 0;
			return true;
		}
		else {
			return false;
		}
	}
};

template<typename T> 
generator<T&> operator|(generator<T&> g, thin t) {
	for(auto& x : g) {
		if(++t)	co_yield x; 
	}
}


struct burn {
	size_t m;
	size_t cnt;
	
	burn(size_t _m) : m(_m), cnt(0) { 
	}

	// funny little increment here returns true for 0 mod m
	bool operator++() {
		if(cnt > m or m==0) {
			return true;
		}
		else {
			++cnt;
			return false;
		}
	}
};

template<typename T> 
generator<T&> operator|(generator<T&> g, burn t) {
	for(auto& x : g) {
		if(++t)	co_yield x; 
	}
}

struct printer { 
	size_t every;
	size_t cnt;
	std::string prefix; 
	
	printer(size_t e, const std::string p="") : every(e), cnt(0), prefix(p) { }

	// funny little increment here returns true for when we print
	bool operator++() {
		if(++cnt == every and every!=0) {
			cnt = 0;
			return true;
		}
		else {
			return false;
		}
	}
};

template<typename T>
struct show_statistics { 
	size_t every;
	size_t cnt;
	T& sampler;
	
	show_statistics(size_t e, T& m) : every(e), cnt(0), sampler(m) { }

	// funny little increment here returns true for when we print
	bool operator++() {
		if(++cnt == every and every!=0) {
			cnt = 0;
			return true;
		}
		else {
			return false;
		}
	}
};


template<typename T> 
generator<T&> operator|(generator<T&> g, printer t) {
	for(auto& x : g) {
		if(++t) x.show(t.prefix);
		co_yield x;
	}
}


template<typename T, typename Q> 
generator<T&> operator|(generator<T&> g, show_statistics<Q> t) {
	for(auto& x : g) {
		if(++t){
			t.sampler.show_statistics();
		}
		co_yield x;
	}
}




// chaining together generator and TopN
#include "TopN.h"

template<typename T> 
generator<T&> operator|(generator<T&> g, TopN<T>& tn) { // NOTE: tn MUST be a ref here...
	for(auto& x : g) {
		tn << x; // add
		co_yield x; // and yield 
	}
}

#include "ReservoirSample.h"

template<typename T> 
generator<T&> operator|(generator<T&> g, ReservoirSample<T>& tn) { // NOTE: tn MUST be a ref here...
	for(auto& x : g) {
		tn << x; // add
		co_yield x; // and yield 
	}
}

template<typename T> 
generator<T&> operator|(generator<T&> g, std::vector<T>& tn) { // NOTE: tn MUST be a ref here...
	for(auto& x : g) {
		tn.push_back(x);
		co_yield x; // and yield 
	}
}


template<typename T> 
generator<T&> operator|(generator<T&> g, std::set<T>& tn) { // NOTE: tn MUST be a ref here...
	for(auto& x : g) {
		tn.add(x);
		co_yield x; // and yield 
	}
}

