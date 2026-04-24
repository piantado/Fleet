#pragma once 

using constant_t = float; 

/**
 * @class Constant
 * @author Steven Piantadosi
 * @date 17/08/23
 * @file ConstantContainer.h
 * @brief This is a struct to basically hold a double (or float) for use in SymbolicRegression etc
 * 		  This allows us to define rules that specifically take Constants instead of doubles/floats
 * 		  which is useful in e.g. linear regression type parts of equations
 */
struct Constant {
	
	float value;
	Constant() : value(0) {}
	Constant(constant_t v) : value(v) {}
	
	void operator=(const constant_t v) {
		value = v; 
	}
	constant_t get_value() const { return value; }
	operator constant_t() const { return value; }
};
