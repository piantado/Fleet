#pragma once

/* 
 * A datum is the default data type for a given input and output */
template<typename t_input, typename t_output>
class default_datum { // a single data point
public:
	t_input input;
	t_output output;
	double   reliability; // the noise probability (typically required)
	
	default_datum() { };
	default_datum(const t_input& i, const t_output& o, double r) : input(i), output(o), reliability(r) {};
	default_datum(const t_input& i, const t_output& o) : input(i), output(o), reliability(NaN) {};	
	
	bool operator==(const default_datum& y) const {
		return input==y.input and output==y.output and reliability==y.reliability;
	}
	
}; 
