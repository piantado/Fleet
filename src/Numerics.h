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