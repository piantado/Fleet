#pragma once 

#include <vector>

/**
 * @class Vector2D
 * @author Steven Piantadosi
 * @date 09/08/20
 * @brief Just a little wrapper to allow vectors to be handled as 2D arrays, which simplifie some stuff in GrammarHypothesis
 */
template<typename T>
struct Vector2D {
	int xsize = 0;
	int ysize = 0;
	std::vector<T> value; 
	
	Vector2D() { }
	
	Vector2D(int x, int y) { 
		resize(x,y);
	}
	
	Vector2D(int x, int y, T b) { 
		resize(x,y);
		fill(b);
	}
	
	void fill(T x){
		std::fill(value.begin(), value.end(), x);
	}
	
	void resize(const int x, const int y) {
		xsize=x; ysize=y;
		value.resize(x*y);
	}
	
	void reserve(const int x, const int y) {
		xsize=x; ysize=y;
		value.reserve(x*y);
	}
	
	T& at(const int x, const int y) {
		return value.at(x*ysize + y);
	}
	
	T& operator()(const int x, const int y) {
		return value.at(x*ysize + y);
	}
	
	const T& operator()(const int x, const int y) const {
		return value.at(x*ysize + y);
	}
	
	template<typename X>
	void operator[](X x) {
		CERR "**** Cannot use [] with Vector2d, use .at()" ENDL;
		throw YouShouldNotBeHereError();
	}
};
