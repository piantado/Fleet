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
// A fater logarithm 
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

/////////////////////////////////////////////////////////////
// Pairing function for integer encoding of trees
// NOTE: These generally should have the property that
// 		 they map 0 to 0 
/////////////////////////////////////////////////////////////

typedef size_t enumerationidx_t; // this is the type we use to store enuemration indices
//namespace Fleet {
//	namespace Enumeration {

		std::pair<enumerationidx_t, enumerationidx_t> cantor_decode(const enumerationidx_t z) {
			enumerationidx_t w = (enumerationidx_t)std::floor((std::sqrt(8*z+1)-1.0)/2);
			enumerationidx_t t = w*(w+1)/2;
			return std::make_pair(z-t, w-(z-t));
		}

		std::pair<enumerationidx_t, enumerationidx_t> rosenberg_strong_decode(const enumerationidx_t z) {
			// https:arxiv.org/pdf/1706.04129.pdf
			enumerationidx_t m = (enumerationidx_t)std::floor(std::sqrt(z));
			if(z-m*m < m) {
				return std::make_pair(z-m*m,m);
			}
			else {
				return std::make_pair(m, m*(m+2)-z);
			}
		}
		
		enumerationidx_t rosenberg_strong_encode(const enumerationidx_t x, const enumerationidx_t y){
			auto m = std::max(x,y);
			return m*(m+1)+x-y;
		}

		std::pair<enumerationidx_t,enumerationidx_t> mod_decode(const enumerationidx_t z, const enumerationidx_t k) {
			
			if(z == 0) 
				return std::make_pair(0,0); // I think?
			
			auto x = z%k;
			return std::make_pair(x, (z-x)/k);
		}

		enumerationidx_t mod_encode(const enumerationidx_t x, const enumerationidx_t y, const enumerationidx_t k) {
			assert(x < k);
			return x + y*k;
		}
		
		// Stack-like operations on z
		enumerationidx_t rosenberg_strong_pop(enumerationidx_t &z) {
			// https:arxiv.org/pdf/1706.04129.pdf
			enumerationidx_t m = (enumerationidx_t)std::floor(std::sqrt(z));
			if(z-m*m < m) {
				auto ret = z-m*m;
				z = m;
				return ret;
			}
			else {
				auto ret = m;
				z = m*(m+2)-z;
				return ret;
			}
		}
		enumerationidx_t mod_pop(enumerationidx_t& z, const enumerationidx_t k) {			
			auto x = z%k;
			z = (z-x)/k;
			return x;
		}
		

//		enumerationidx_t rosenberg_strong_pop(enumerationidx_t& z) {
//			u = rosenberg_strong_decode(z);
//			z = u.first;
//			return u.second;
//		}
//		enumerationidx_t mod_pop(enumerationidx_t& z) {
//			u = rosenberg_strong_decode(z);
//			z = u.first;
//			return u.second;
//		}
	
//	}
//}