#pragma once 

#include <vector>

/**
 * @class Vector3D
 * @author Steven Piantadosi
 * @date 09/08/20
 * @brief Like Vector2D, but one dimension bigger. 
 * 		  TODO: Replace this and Vector2D with a template please.
 */
template<typename T>
struct Vector3D {
	int xsize = 0;
	int ysize = 0;
	int zsize = 0;
	std::vector<T> value; 
	
	Vector3D() { }
	
	Vector3D(int x, int y, int z) { 
		resize(x,y,z);
	}
	
	void resize(int x, int y, int z) {
		xsize = x; ysize=y; zsize=z;
		value.resize(x*y*z);
	}
	
	void reserve(int x, int y, int z) {
		xsize = x; ysize=y; zsize=z;
		value.reserve(x*y*z);
	}
	
	T& at(int x, int y, int z) {
		// NOTE: this indexing makes iteration over z the fastest
		return value.at((x*ysize + y)*zsize + z);
	}
	
	template<typename X>
	void operator[](X x) {
		CERR "**** Cannot use [] with Vector2d, use .at()" ENDL;
		throw YouShouldNotBeHereError();
	}
};
