#pragma once

#include <random>
#include <iostream>
#include <assert.h>

/////////////////////////////////////////////////////////////
// Constants
/////////////////////////////////////////////////////////////

const double LOG05 = -log(2.0);
const double infinity = std::numeric_limits<double>::infinity();
const double NaN = std::numeric_limits<double>::quiet_NaN();
const double pi  = M_PI;

/////////////////////////////////////////////////////////////
// Probability
/////////////////////////////////////////////////////////////

template<typename t>
t logplusexp(const t a, const t b) {
	// An approximate logplusexp -- the check on z<-25 makes it much faster since it saves many log,exp operations
	// It is easy to derive a good polynomial approximation that is a bit faster (using sollya) on [-25,0] but that appears
	// not to be worth it at this point. 
	
	if     (a == -infinity) return b;
	else if(b == -infinity) return a;
	
	t mx = std::max(a,b);
//	return mx + log(exp(a-mx)+exp(b-mx));
	
	t z  = std::min(a,b)-mx;
	if(z < -25.0) return mx;

	return mx + log1p(exp(z));
    //return mx + log(exp(a-mx)+exp(b-mx));
}

template<typename t>
double logsumexp(const t& v) {
	double lse = -infinity;
	for(auto& x : v) {
		lse = logplusexp(lse,x);
	}
	return lse;
}