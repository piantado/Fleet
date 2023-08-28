#pragma once

// This includes our thread-safe lgamma and must be declared before math.h is imported
// This means that we should NOT import math.h anywhere else
#define _REENTRANT 1 

#include <map>
#include <random>
#include <iostream>
#include <assert.h>
#include <algorithm>
#include <math.h>

/////////////////////////////////////////////////////////////
// Constants
/////////////////////////////////////////////////////////////

const double LOG2         = log(2.0); // clang doesn't like constexpr??
const double ROOT2        = sqrt(2.0);
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double NaN      = std::numeric_limits<double>::quiet_NaN();
constexpr double pi       = M_PI;
constexpr double tau      = 2*pi; // fuck pi

/////////////////////////////////////////////////////////////
// Rounding 
/////////////////////////////////////////////////////////////

template<typename T>
T round(T v, int n) {
	auto m = pow(10,n);
	return std::round(v*m)/m;
}

/////////////////////////////////////////////////////////////
// A faster (?) logarithm 
/////////////////////////////////////////////////////////////

//float __int_as_float(int32_t a) { float r; memcpy(&r, &a, sizeof(r)); return r;}
//int   __float_as_int(float a) { int r; memcpy(&r, &a, sizeof(r)); return r;}
//
///* natural log on [0x1.f7a5ecp-127, 0x1.fffffep127]. Maximum relative error 9.4529e-5 */
//// From https://stackoverflow.com/questions/39821367/very-fast-approximate-logarithm-natural-log-function-in-c
//float fastlogf(float a) {
//    float m, r, s, t, i, f;
//    int32_t e;
//
//    e = (__float_as_int(a) - 0x3f2aaaab) & 0xff800000;
//    m = __int_as_float (__float_as_int(a) - e);
//    i = (float) e * 1.19209290e-7f; // 0x1.0p-23
//    /* m in [2/3, 4/3] */
//    f = m - 1.0f;
//    s = f * f;
//    /* Compute log1p(f) for f in [-1/3, 1/3] */
//    r = fmaf(0.230836749f, f, -0.279208571f); // 0x1.d8c0f0p-3, -0x1.1de8dap-2
//    t = fmaf(0.331826031f, f, -0.498910338f); // 0x1.53ca34p-2, -0x1.fee25ap-2
//    r = fmaf(r, s, t);
//    r = fmaf(r, s, f);
//    r = fmaf(i, 0.693147182f, r); // 0x1.62e430p-1 // log(2) 
//    return r;
//}


/////////////////////////////////////////////////////////////
// Probability
/////////////////////////////////////////////////////////////


/**
 * @brief Tis takes a function f(x) and finds a bound B so that f(x) < precision for all x < B.
 *        This is useful in logsumexp where at compile time we don't bother adding if the addition will be too tiny. 
 *        For instance: 
 * 				constexpr auto f = +[](T x) -> const double {return log1p(exp(x));}; 	
 * 				const T bound = get_fx_compiletime_bound<T>(1e-6, f);
 * @param precision
 * @param f
 * @param lower
 * @param upper
 */
template<typename T>
T get_log1pexp_breakout_bound(const double precision, T f(T), const T lower=-1e6, const T upper=1e6) {
	
	assert(f(lower) > precision or f(upper) > precision);
	
	const T m = lower + (upper-lower)/2.0;
	if (upper-lower < 1e-3) {
		return m;
	}
	else if (f(m) < precision) { // bound on precision here 
		return get_log1pexp_breakout_bound<T>(precision, f, m, upper);
	}
	else {
		return get_log1pexp_breakout_bound<T>(precision, f, lower, m);
	}
}


template<typename T>
T logplusexp(const T a, const T b) {
	// An approximate logplusexp -- the check on z<-25 makes it much faster since it saves many log,exp operations
	// It is easy to derive a good polynomial approximation that is a bit faster (using sollya) on [-25,0] but that appears
	// not to be worth it at this point. 
	
	if     (a == -infinity) return b;
	else if(b == -infinity) return a;
	
	T mx = std::max(a,b);
	
	// compute a bound here at such that log1p(exp(z)) doesn't affect the result much (up to 1e-6)
	// for floats, this ends up being about -13.8
	// TODO: I Wish we could make this constexpr, but the math functions are not constexpr. Static is fine though
	// it should only be run once at the start.
	static const float breakout = get_log1pexp_breakout_bound<T>(1e-6, +[](T x) -> T { return log1p(exp(x)); });

	T z  = std::min(a,b)-mx;
	if(z < breakout) {
		return mx; // save us from having to do anything
	}
	else {
		return mx + log1p(exp(z));   // mx + log(exp(a-mx)+exp(b-mx));
	}
}

/**
 * @brief logsumexp with no shortcuts for precision
 * @param a
 * @param b
 * @return 
 */
template<typename T>
T logplusexp_full(const T a, const T b) {
	T mx = std::max(a,b);
	return mx + log1p(exp(std::min(a,b)-mx));
}


/**
 * @brief Compute log(sum(exp(v)).
 * 		  For now, this just unrolls logplusexp, but we might consider the faster (standard) variant where we pull out the max.
 * @param v
 * @return 
 */
template<typename t>
double logsumexp(const t& v) {
	double lse = -infinity;
	for(auto& x : v) {
		lse = logplusexp(lse,x);
	}
	return lse;
}

template<typename t>
double logsumexp(const std::vector<t>& v, double f(const t&) ) {
	double lse = -infinity;
	for(auto& x : v) {
		lse = logplusexp(lse,f(x));
	}
	return lse;
}

template<typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

/////////////////////////////////////////////////////////////
// Numerical functions
/////////////////////////////////////////////////////////////

double mylgamma(double v) {
	// thread safe version gives our own place to store sgn
	int sgn = 1;
	auto ret = lgamma_r(v, &sgn);
	return ret;
}

double mygamma(double v) {
	// thread safe usage
	int sgn = 1;
	auto ret = lgamma_r(v, &sgn);
	return sgn*exp(ret);
}

double lfactorial(double x) {
	return mylgamma(x+1);
}

/////////////////////////////////////////////////////////////
/// logic / inverse logit are pretty useful
/////////////////////////////////////////////////////////////

template<typename T>
T inv_logit(const T z) {
	return 1.0 / (1.0 + exp(-z));
}

template<typename T>
T logit(const T p) {
	return log(p)-log(1-p);
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// approximate equality 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
bool approx_eq(const T x, const T y, double prec=0.00001) {
	return abs(x-y)<prec;
}


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Weighted quantiles
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
T weighted_quantile(std::vector<std::pair<T,double>>& v, double q) {
	// find the weighted quantile, from a list of pairs <T,logprobability>
	// NOTE: may modify v..
	
	std::sort(v.begin(), v.end()); // sort by T 
	
	// get the normalizer (log space)
	double z = -infinity;
	for(auto [x,lp] : v) z = logplusexp(z,lp);
	
	double cumulative = -infinity;
	for(auto [x,lp] : v) {
		cumulative = logplusexp(cumulative, lp);
		if(exp(cumulative-z) >= q) {
			return x;
		}
	}
	
	assert(false);
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Mean, sd
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
T sum(std::vector<T>& v){
	T s = 0.0;
	for(auto& x : v) s += x;
	return s;
}

template<typename T>
T mean(std::vector<T>& v){
	return sum(v)/v.size();
}

template<typename T>
T sd(std::vector<T>& v) {
	assert(v.size() > 1);
	T m = mean(v);
	T s = 0.0;
	for(auto& x : v) {
		s += pow(m-x,2.);
	}
	return sqrt(s / (v.size()-1));
}

template<typename T>
T quantile(std::vector<T>& v, double q) {
	const size_t n = v.size();
	assert(n> 0);
	std::sort(v.begin(), v.end());
	
	int i = floor(n*q); // floor for small n
	return v.at(i);
}

template<typename T>
T median(std::vector<T>& v) {
	const size_t n = v.size();
	assert(n> 0);
	std::sort(v.begin(), v.end());
	
	if(n % 2 == 0) {
		return (v[n/2] + v[n/2-1]) / 2;
	}
	else {
		return v[n/2];
	}
}


template<typename T>
T trimmed_mean(std::vector<T>& v, float pct) {
	const size_t n = v.size();
	assert(n> 0);
	std::sort(v.begin(), v.end());
	assert(pct < 0.5 && "*** Bad range in trimmed_mean");
	
	double s = 0.0;
	double k = 0;
	for(int i=pct*n;i<=(1-pct)*n;i++) {
		s += v.at(i); 
		k++;
	}
	return s/k;	
}


template<typename T>
T trimmed_mean(std::vector<T>& v, float a, float b) {
	const size_t n = v.size();
	assert(n> 0);
	std::sort(v.begin(), v.end());
	assert(a >= 0.0 and b <= 1.0 and a<b && "*** Bad range in trimmed_mean");
	
	double s = 0.0;
	double k = 0;
	for(int i=a*n;i<=b*n;i++) {
		s += v.at(i); 
		k++;
	}
	return s/k;	
}


template<typename T>
T mymax(std::vector<T>& v) {
	const size_t n = v.size();
	assert(n> 0);
	
	auto m = v[0];
	for(size_t i=0;i<n;i++) 
		m = std::max(v[i], m);
		
	return m;
}


/**
 * @brief Get max of the values
 * @param v
 * @return 
 */
template<typename T, typename K>
K mymax(std::map<T,K>& v) {
	auto m = v.begin()->second;
	for(const auto& x : v) {
		m = std::max(x.second, m);
	}
	return m;
}


/**
 * @brief This allows sorting of things that contain NaN
 * @param x
 * @param y
 * @return 
 */
template<typename T>
struct floating_point_compare {
	bool operator()(const T &x, const T &y) const {
		if(std::isnan(x)) return false;
		if(std::isnan(y)) return true;
		return x < y;
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Hash combinations
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T> 
std::strong_ordering fp_ordering(T& x, T& y) {
/**
 * @brief Convert a std::partial_ordering on x and y (floats or doubles) into a strong one by sorting NaN to last.
 * 		  so that fp_ordering(x,y) is the same as (x<=>y) with NaN sorting to last, and NaNs comparing to equal 
 * @param x
 * @param y
 * @return 
 */
	if( std::isnan(x) and std::isnan(y)) { 
		return std::strong_ordering::equivalent; 
	}
	else if( std::isnan(x) ) {
		return std::strong_ordering::less;
	}
	else if (std::isnan(y)) {
		return std::strong_ordering::greater;
	}
	else {
		auto v = (x <=> y);
		
		if     (v == std::partial_ordering::less)       return std::strong_ordering::less;
		else if(v == std::partial_ordering::greater)    return std::strong_ordering::greater;
		else if(v == std::partial_ordering::equivalent) return std::strong_ordering::equivalent;
		else assert(false);
	}
}