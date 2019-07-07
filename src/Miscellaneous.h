#pragma once

#include <vector>

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start, int len) {
	// slice of a vector
	std::vector<T> out;
	if(len > 0) {
		out.resize(len); // in case len=-1
		std::copy(v.begin() + start, v.begin()+start+len, out.begin());
	}
	return out;
}

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start) {
	// just chop off the end 
	return slice(v, start, v.size()-start);
}