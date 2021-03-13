#pragma once

#include<assert.h>

#include "Object.h"
#include "Strings.h"
#include "IO.h"

// Define features
enum class Shape { Rectangle, Triangle, Circle};
enum class Color { Blue, Yellow, Green};
enum class Size  { size1, size2, size3};

/**
 * @class ShapeColorSizeObject
 * @author Steven Piantadosi
 * @date 11/09/20
 * @file ShapeColorSizeObject.h
 * @brief This object is used in FOL and the set function experiments -- just a handy function with a few convenient features
 */
struct ShapeColorSizeObject : public Object<Shape,Color,Size>  {
	using Super = Object<Shape,Color,Size>;
	using Super::Super;
	
	ShapeColorSizeObject() {}
	
	/**
	 * @brief Mainly we just define a constructor which takes strings
	 * @param _shape
	 * @param _color
	 * @param _size
	 */		
	ShapeColorSizeObject(const std::string _shape, const std::string _color, const std::string _size) {
		set(_shape, _color, _size);
	}
	
	ShapeColorSizeObject(const std::string s) {
		auto [_shape, _color, _size] = split<3>(s, '-');
		set(_shape,_color,_size);	
	}
	
	void set(const std::string _shape, const std::string _color, const std::string _size) {
		// NOT: we could use magic_enum, but haven't here to avoid a dependency
		if     (_shape == "triangle")   Super::set(Shape::Triangle);
		else if(_shape == "rectangle")  Super::set(Shape::Rectangle);
		else if(_shape == "circle")     Super::set(Shape::Circle);
		else { CERR _shape ENDL; assert(0); }
		
		if     (_color == "blue")     Super::set(Color::Blue);
		else if(_color == "yellow")   Super::set(Color::Yellow);
		else if(_color == "green")    Super::set(Color::Green);
		else { CERR _color ENDL; assert(0); }
		
		if     (_size == "1")        Super::set(Size::size1);
		else if(_size == "2")        Super::set(Size::size2);
		else if(_size == "3")        Super::set(Size::size3);
		else { CERR _size ENDL; assert(0); }
	}
};

