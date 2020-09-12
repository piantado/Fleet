#pragma once 

#include <string>

enum class Shape { rectangle, triangle, circle};
enum class Color { yellow, green, blue};
enum class Size  { size1, size2, size3};

using S = std::string;

/**
 * @class MyObject
 * @author Steven Piantadosi
 * @date 11/09/20
 * @file MyObject.h
 * @brief This is for GrammarInference-SetFunctionLearning and RationalRules since they both use that data.
 */
struct MyObject : public Object<Shape,Color,Size>  {
	
	MyObject() {}
	
	/**
	 * @brief Mainly we just define a constructor which takes strings
	 * @param _shape
	 * @param _color
	 * @param _size
	 */		
	MyObject(S _shape, S _color, S _size) {
		// NOT: we could use magic_enum, but haven't here to avoid a dependency
		if     (_shape == "triangle")   this->set(Shape::triangle);
		else if(_shape == "rectangle")  this->set(Shape::rectangle);
		else if(_shape == "circle")     this->set(Shape::circle);
		else assert(0);
		
		if     (_color == "blue")     this->set(Color::blue);
		else if(_color == "yellow")   this->set(Color::yellow);
		else if(_color == "green")    this->set(Color::green);
		else assert(0);
		
		if     (_size == "1")        this->set(Size::size1);
		else if(_size == "2")        this->set(Size::size2);
		else if(_size == "3")        this->set(Size::size3);
		else assert(0);
	}
	
};
