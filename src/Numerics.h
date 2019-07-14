#pragma once

#include <random>

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
	
    return mx + log(exp(a-mx)+exp(b-mx));
}
