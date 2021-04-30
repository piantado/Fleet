
#pragma once

#include <coroutine>
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
generator<T> operator|(generator<T> g, thin t) {
	for(auto& x : g) {
		if(++t)	co_yield x; 
	}
}

// chaining together generator and TopN
#include "Top.h"
template<typename T> 
generator<T> operator|(generator<T> g, TopN<T>& tn) { // NOTE: tn MUST be a ref here...
	for(auto& x : g) {
		tn << x; // add
		co_yield x; // and yield 
	}
}




