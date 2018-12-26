#pragma once

// Define a default data type 
template<typename t_input, typename t_output>
class default_datum { // a single data point
public:
	t_input input;
	t_output output;
	double   reliability; // the noise probability (typically required)
	
	default_datum(t_input i, t_output o, double r) : input(i), output(o), reliability(r) {};
	default_datum(t_input i, t_output o) : input(i), output(o), reliability(NaN) {};		
}; 
