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
		set_size(x,y);
	}
	
	void set_size(int x, int y) {
		xsize = x;
		ysize = y;
	}
	
	void reserve(int x, int y) {
		set_size(x,y);
		value.reserve(x*y);
	}
	
	T& at(int x, int y) {
		return value.at(x*ysize + y);
	}
};
