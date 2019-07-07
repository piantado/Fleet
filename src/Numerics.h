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

//double logplusexp(const double a, const double b) {
//    if(a==-infinity) return b;
//    if(b==-infinity) return a;
//    
//    double m = (a>b?a:b);
//    return m + log(exp(a-m)+exp(b-m));
//}
//float logplusexpf(const float a, const float b) {
//	// This ends up being very expensie to do in double precision
//	// so in some critical places (e.g. in FormalLanguageTheory) we use the float version with -fast-math, which is much faster
//    if(a==-infinity) return b;
//    if(b==-infinity) return a;
//    
//    float m = (a>b?a:b);
//    return m + log(exp(a-m)+exp(b-m));
//}

template<typename t>
t logplusexp(const t a, const t b) {
	// An approximate logplusexp -- the check on z<-25 makes it much faster since it saves many log,exp operations
	t mx = MAX(a,b);
	t z  = MIN(a,b)-mx;
	if(z < -25.0) return mx;
	
    return mx + log(exp(a-mx)+exp(b-mx));
}

/////////////////////////////////////////////////////////////
// Random 
/////////////////////////////////////////////////////////////

std::random_device rd;     // only used once to initialise (seed) engine
std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
std::uniform_real_distribution<double> uniform_dist(0,1.0);

double uniform() {
	return uniform_dist(rng);
}

double cauchy_lpdf(double x, double loc=0.0, double gamma=1.0) {
    return -log(pi) - log(gamma) - log(1+((x-loc)/gamma)*((x-loc)/gamma));
}

double normal_lpdf(double x, double mu=0.0, double sd=1.0) {
    //https://stackoverflow.com/questions/10847007/using-the-gaussian-probability-density-function-in-c
    const float linv_sqrt_2pi = -0.5*log(2*pi*sd);
    return linv_sqrt_2pi  - ((x-mu)/sd)*((x-mu)/sd) / 2.0;
}

double random_cauchy() {
	return tan(pi*(uniform()-0.5));
}

template<typename t>
std::vector<t> random_multinomial(t alpha, size_t len) {
	// give back a random multinomial distribution 
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
	std::uniform_int_distribution<T> r(0,max-1);
	return r(rng);
}

bool flip() {
	return uniform() < 0.5;
}