#pragma once

typedef size_t enumerationidx_t; // this is the type we use to store enuemration indices

/**
 * @class IntegerizedStack
 * @author piantado
 * @date 09/06/20
 * @file Enumeration.h
 * @brief An IntegerizedStack is just a wrapper around unsigned longs that allow them to behave like a of integers stack, supporting push and pop (maybe with mod) via a standard pairing function
 */ 
class IntegerizedStack {
	typedef unsigned long long value_t;

protected:
	value_t value;
	
public:
	IntegerizedStack(value_t v=0) : value(v) {		
	}
	
	// A bunch of static operations
	
	//std::pair<enumerationidx_t, enumerationidx_t> cantor_decode(const enumerationidx_t z) {
	//	enumerationidx_t w = (enumerationidx_t)std::floor((std::sqrt(8*z+1)-1.0)/2);
	//	enumerationidx_t t = w*(w+1)/2;
	//	return std::make_pair(z-t, w-(z-t));
	//}

	static std::pair<enumerationidx_t, enumerationidx_t> rosenberg_strong_decode(const enumerationidx_t z) {
		// https:arxiv.org/pdf/1706.04129.pdf
		enumerationidx_t m = (enumerationidx_t)std::floor(std::sqrt(z));
		if(z-m*m < m) {
			return std::make_pair(z-m*m,m);
		}
		else {
			return std::make_pair(m, m*(m+2)-z);
		}
	}

	static enumerationidx_t rosenberg_strong_encode(const enumerationidx_t x, const enumerationidx_t y){
		auto m = std::max(x,y);
		return m*(m+1)+x-y;
	}

	static std::pair<enumerationidx_t,enumerationidx_t> mod_decode(const enumerationidx_t z, const enumerationidx_t k) {
		
		if(z == 0) 
			return std::make_pair(0,0); // I think?
		
		auto x = z % k;
		return std::make_pair(x, (z-x)/k);
	}

	static enumerationidx_t mod_encode(const enumerationidx_t x, const enumerationidx_t y, const enumerationidx_t k) {
		assert(x < k);
		return x + y*k;
	}

	// Stack-like operations on z
	static enumerationidx_t rosenberg_strong_pop(enumerationidx_t &z) {
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
	static enumerationidx_t mod_pop(enumerationidx_t& z, const enumerationidx_t k) {			
		auto x = z%k;
		z = (z-x)/k;
		return x;
	}
	
	
	value_t pop() {
		auto u = rosenberg_strong_decode(value);
		value = u.second;
		return u.first;
	}

	value_t pop(value_t modulus) { // for mod decoding
		auto u = mod_decode(value, modulus);
		value = u.second;
		return u.first;
	}
	
	void push(value_t x) {
		auto before = value;
		value = rosenberg_strong_encode(x, value);
		assert(before <= value && "*** Overflow in encoding IntegerizedStack::push");
	}

	void push(value_t x, value_t modulus) {
		auto before = value;
		value = mod_encode(x, value, modulus);
		assert(before <= value && "*** Overflow in encoding IntegerizedStack::push");
	}
	
	/**
	 * @brief Split into n children -- this is the same as looping and popping, but provides a nicer interface
	 * 		  NOTE here that this is *not* just n pops -- the last remaining amount is returned as the last
	 *        value in the vector. 
	 * 		  NOTE: After doing this, the value=0 (since it's been poped into split)
	 * @param n
	 * @return 
	 */	
	std::vector<value_t> split(size_t n) {
		std::vector<value_t> out(n);
		for(size_t i=0;i<n-1;i++) {
			out[i] = pop();
		}
		out[n-1] = get_value();
		value = 0; // we remove that last value so nobody gets confused, although the stack really shouldn't be accessed after this
		return out;
	}
	
	value_t get_value() const {
		return value; 
	}
	
	bool empty() const {
		// not necessarily empty, just will forever return 0
		return value == 0;
	}
	
	void operator=(value_t z) {
		// just set my value
		value = z;
	}
	void operator-=(value_t x) {
		// subtract off my value
		value -= x;
	}
	void operator+=(value_t x) {
		// add to value
		value += x;
	}
};
std::ostream& operator<<(std::ostream& o, const IntegerizedStack& n) {
	o << n.get_value();
	return o;
}	