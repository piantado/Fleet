#pragma once

class IntegerizedStack {
	// An IntegerizedStack is just a wrapper around unsigned longs that allow
	// them to behave like a stack, supporting push and pop (maybe with mod)
	// via a standard pairing function
	// (The name is because its not the same as an integer stack)
	
	typedef unsigned long value_t;

protected:
	value_t value;
	
public:
	IntegerizedStack(value_t v=0) : value(v) {
		
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
		assert(before < value);
	}

	void push_mod(value_t x, value_t modulus) {
		auto before = value;
		value = mod_encode(x, value, modulus);
		assert(before < value);
	}
	
	value_t get_value() {
		return value; 
	}
	
	bool empty() const {
		// not necessarily empty, just will forever return 0
		return value == 0;
	}
};

