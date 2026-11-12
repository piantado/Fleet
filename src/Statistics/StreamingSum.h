#pragma once 

template<typename T>
struct StreamingSum {
	T total; 
	T low; // low order bits 
	// TODO: Maybe lets pick an even better version? https://en.wikipedia.org/wiki/Kahan_summation_algorithm
	StreamingSum(const T v=0) : total(v), low(0) { }
	
	void reset() {
		total = 0;
		low = 0;
	}
	
	T operator<<(const T x) {
		T y = x - low; 
		volatile T t = total + y; // see here: https://en.wikipedia.org/wiki/Kahan_summation_algorithm
		volatile T z = t-total; 
		low = z-y; 
		
		total = t; 
		
		return x; // NOT the total 
	}
	
	T operator=(const T v) {
		total = v;
		low = 0;
		return v; 
	}
	
	T operator+=(const T v) {
		return this->operator<<(v); 
	}
	
	// NOTE: total - low. low is Kahan's compensation: what was added to total in EXCESS of what should 
	// have been (it is subtracted from the next term). This used to return total + low, which doubles
	// the pending error instead of removing it. 
	operator double() const { return (double)total - (double)low; } // (in double, so that a float sum keeps the correction)
};


/**
 * @class VanillaStreamingSum
 * @author Steven Piantadosi
 * @date 31/05/24
 * @file StreamingSum.h
 * @brief Mostly for error checking
 */

template<typename T>
struct VanillaStreamingSum {
	T total; 
	
	VanillaStreamingSum() : total(0) { }
	
	T operator<<(T x) {
		total += x;	
		
		return x; // NOT the total 
	}
	
	operator double() const { return double(total); }
};



template<typename T> 
struct StreamingMean {
	size_t n;
	StreamingSum<T> sum;

	StreamingMean() : n(0) {
	}
	
	void reset() {
		n = 0;
		sum.reset();
	}
	
	T operator<<(T v) {
		sum << v; 
		this->n++;
		return v; 
	}
	
	operator T() const {
		return T(sum)/T(this->n); // NOTE: Doesn't behave right if ints...
	}
	
//	float () {
//		return double(this->s)/double(this->n);
//	}
	
};