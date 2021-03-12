#pragma once

#include "Strings.h"

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
	ShapeColorSizeObject(S _shape, S _color, S _size) {
		// NOT: we could use magic_enum, but haven't here to avoid a dependency
		if     (_shape == "triangle")   this->set(Shape::Triangle);
		else if(_shape == "rectangle")  this->set(Shape::Rectangle);
		else if(_shape == "circle")     this->set(Shape::Circle);
		else assert(0);
		
		if     (_color == "blue")     this->set(Color::Blue);
		else if(_color == "yellow")   this->set(Color::Yellow);
		else if(_color == "green")    this->set(Color::Green);
		else assert(0);
		
		if     (_size == "1")        this->set(Size::size1);
		else if(_size == "2")        this->set(Size::size2);
		else if(_size == "3")        this->set(Size::size3);
		else assert(0);
	}
};

// Define this specialization
template<> ShapeColorSizeObject string_to(const std::string s) {
	auto [shape, color, size] = split<3>(s, '-');
	return ShapeColorSizeObject(shape,color,size);	
}