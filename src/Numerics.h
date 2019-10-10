#pragma once

#include <random>
#include <iostream>

/* Handy numeric functions */

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
	 
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _b : _a; })

/////////////////////////////////////////////////////////////
// Constants
/////////////////////////////////////////////////////////////

const double LOG05 = -log(2.0);
const double infinity = std::numeric_limits<double>::infinity();
const double NaN = std::numeric_limits<double>::quiet_NaN();;
const double pi  = 3.141592653589793238;

/////////////////////////////////////////////////////////////
// Probability
/////////////////////////////////////////////////////////////

template<typename t>
t logplusexp(const t a, const t b) {
	// An approximate logplusexp -- the check on z<-25 makes it much faster since it saves many log,exp operations
	// It is easy to derive a good polynomial approximation that is a bit faster (using sollya) on [-25,0] but that appears
	// not to be worth it at this point. 
	
	t mx = MAX(a,b);
	t z  = MIN(a,b)-mx;
	if(z < -25.0) return mx;

	return mx + log1p(exp(z));
    //return mx + log(exp(a-mx)+exp(b-mx));
}

/////////////////////////////////////////////////////////////
// Pairing function for integer encoding of trees
// NOTE: These generally should have the property that
// 		 they map 0 to 0 
/////////////////////////////////////////////////////////////

std::pair<size_t, size_t> cantor_decode(const size_t z) {
	size_t w = (size_t)std::floor((std::sqrt(8*z+1)-1.0)/2);
	size_t t = w*(w+1)/2;
	return std::make_pair(z-t, w-(z-t));
}

std::pair<size_t, size_t> rosenberg_strong_decode(const size_t z) {
	// https:arxiv.org/pdf/1706.04129.pdf
	size_t m = (size_t)std::floor(std::sqrt(z));
	if(z-m*m < m) {
		return std::make_pair(z-m*m,m);
	}
	else {
		return std::make_pair(m, m*(m+2)-z);
	}
}

size_t rosenberg_strong_encode(const size_t x, const size_t y){
	auto m = std::max(x,y);
	return m*(m+1)+x-y;
}

std::pair<size_t,size_t> mod_decode(const size_t z, const size_t k) {
	auto x = z%k;
	return std::make_pair(x, (z-x)/k);
}