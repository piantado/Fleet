#pragma once

// Define a default data type 
template<typename t_input, typename t_output>
class default_datum { // a single data point
public:
	t_input input;
	t_output output;
	double   reliability; // the noise probability (typically required)
	
	default_datum() { };
	default_datum(const t_input& i, const t_output& o, double r) : input(i), output(o), reliability(r) {};
	default_datum(const t_input& i, const t_output& o) : input(i), output(o), reliability(NaN) {};	
	
	bool operator==(default_datum& y) {
		return input==y.input and output==y.output and reliability==y.reliability;
	}
	
}; 
