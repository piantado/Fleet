#pragma once 

#include <vector>

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start, int len) {
	/**
	 * @brief Take a slice of a vector v starting at start of length len
	 * @param v
	 * @param start
	 * @param len
	 * @return 
	 */
	
	std::vector<T> out;
	if(len > 0) {
		out.resize(len); // in case len=-1
		std::copy(v.begin() + start, v.begin()+start+len, out.begin());
	}
	return out;
}

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start) {
	/**
	 * @brief Take a slice of a vector until its end
	 * @param v
	 * @param start
	 * @return 
	 */
		
	// just chop off the end 
	return slice(v, start, v.size()-start);
}

template<typename T>
void increment(std::vector<T>& v, size_t idx, T count=1) {
	// Increment and potentially increase my vector size if needed
	if(idx >= v.size()) {
		v.resize(idx+1,0);
	}
	
	v[idx] += count;
}
