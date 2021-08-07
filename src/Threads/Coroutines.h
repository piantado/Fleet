
#pragma once

#include <string>
#include <coroutine>
#include "generator.hpp"

// We'll use cppcoro's generator for now, where is std::generator
template<typename T>
using generator = cppcoro::generator<T>;

/**
 * @class every
 * @author Steven Piantadosi
 * @date 11/07/21
 * @file Coroutines.h
 * @brief This little object will return true once every n times it is converted to a bool. 
 */
//template<char... chars>
//struct every { 
//	size_t cnt;
//	size_t n; 
//	every(size_t _n) : n(_n), cnt(0) {	}
//	
//	explicit operator bool() {
//		if(cnt++==n or n==0) {
//			cnt = 0;
//			return true;
//		}
//		else {
//			return false; 
//		}
//	}
//	
//};

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

struct print { 
	size_t every;
	size_t cnt;
	std::string prefix; 
	
	print(size_t e, const std::string p="") : every(e), cnt(0), prefix(p) { }

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
generator<T&> operator|(generator<T&> g, print t) {
	for(auto& x : g) {
		if(++t) x.print(t.prefix);
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
