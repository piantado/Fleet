#pragma once 

#include <vector>
#include <assert.h>
#include "IO.h"

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
	
	bool inbounds(int x, int y) {
		return (x>=0 and x<xsize and y>=0 and y<ysize);
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
	
	void insert_y(const int y_at, T b) {
		// insert a new row at y -- note this is a slow operation I'm sure 
		if(y_at >= ysize+1) return; // nothing happens
		auto nxt = Vector2D<T>(xsize, ysize+1);
		for(int x=0;x<nxt.xsize;x++) {
			for(int y=0;y<nxt.ysize;y++) {
				if(y < y_at)       nxt.at(x,y) = this->at(x,y);
				else if(y == y_at) nxt.at(x,y) = b;
				else               nxt.at(x,y) = this->at(x,y-1);				
			}
		}
		
		this->operator=(std::move(nxt)); // set to myself
	}
	void insert_x(const int x_at, T b) {
		// insert a new row at y -- note this is a slow operation I'm sure 
		if(x_at >= xsize+1) return; // nothing happens
		auto nxt = Vector2D<T>(xsize+1, ysize);
		for(int x=0;x<nxt.xsize;x++) {
			for(int y=0;y<nxt.ysize;y++) {
				if(x < x_at)       nxt.at(x,y) = this->at(x,y);
				else if(x == x_at) nxt.at(x,y) = b;
				else               nxt.at(x,y) = this->at(x-1,y);				
			}
		}
		
		this->operator=(std::move(nxt)); // set to myself
	}
	void delete_y(const int y_at) {
		if(ysize == 0 or y_at >= ysize or y_at < 0) return;
		// insert a new row at y -- note this is a slow operation I'm sure 
		auto nxt = Vector2D<T>(xsize, ysize-1);
		for(int x=0;x<nxt.xsize;x++) { // TODO Can speed up by moving the if outside the y loop
			for(int y=0;y<ysize;y++) {
				if(y < y_at)       nxt.at(x,y) = this->at(x,y);
				else if(y > y_at)  nxt.at(x,y-1) = this->at(x,y);				
			}
		}
		
		this->operator=(std::move(nxt)); // set to myself
	}
	void delete_x(const int x_at) {
		if(xsize == 0 or x_at >= xsize or x_at < 0) return;
		// insert a new row at y -- note this is a slow operation I'm sure 
		auto nxt = Vector2D<T>(xsize-1, ysize);
		for(int x=0;x<xsize;x++) {
			for(int y=0;y<nxt.ysize;y++) {
				if(x < x_at)       nxt.at(x,y) = this->at(x,y);
				else if(x > x_at)  nxt.at(x-1,y) = this->at(x,y);				
			}
		}
		
		this->operator=(std::move(nxt)); // set to myself
	}
	
	
	const T& at(const int x, const int y) const {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	T& at(const int x, const int y) {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	T& operator()(const int x, const int y) const {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	const T& operator()(const int x, const int y) {
		return value.at(x*ysize + y);
	}
	
	
	// get and Set here are used without references (letting us to 2D vectors of bools)
	void set(const int x, const int y, const T& val) {
		value.at(x*ysize + y) = val; 
	}
	void set(const int x, const int y, const T&& val) {
		value.at(x*ysize + y) = val; 
	}
	T get(const int x, const int y) {
		return value.at(x*ysize+y);
	}
	
	template<typename X>
	void operator[](X x) {
		CERR "**** Cannot use [] with Vector2d, use .at()" ENDL;
		throw YouShouldNotBeHereError();
	}
	
	std::string string() {
		std::string out = "[";
		for(int y=0;y<ysize;y++) {
			out += "[";
			for(int x=0;x<xsize;x++) {
				out += str(at(x,y)) + ",";				
			}
			if(ysize > 0) out.pop_back();
			out += "],";
		}
		if(xsize > 0) out.pop_back(); // remove last comma
		return out;
	}
};

/**
 * @class Vector2D
 * @author Steven Piantadosi
 * @date 03/05/22
 * @file Vector2D.h
 * @brief A little trick here to force bools to act like chars and have normal std::vector iterators etc.
 * 		  This wastes space but prevents us from writing other code. 
 */
//
//template<>
//struct Vector2D<bool> : Vector2D<unsigned char> {
//	using Super = Vector2D<unsigned char>;
//	using Super::Super;
//	
//};
