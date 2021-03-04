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
	
	void resize(int x, int y) {
		xsize = x; ysize=y;
		value.resize(x*y);
	}
	
	void reserve(int x, int y) {
		xsize = x; ysize=y;
		value.reserve(x*y);
	}
	
	T& at(int x, int y) {
		return value.at(x*ysize + y);
	}
	
	T& operator()(int x, int y) {
		return value.at(x*ysize + y);
	}
	
	template<typename X>
	void operator[](X x) {
		CERR "**** Cannot use [] with Vector2d, use .at()" ENDL;
		throw YouShouldNotBeHereError();
	}
};
