#pragma once

#include "Strings.h"

/**
* @class defaultdatum_t
* @author piantado
* @date 29/01/20
* @file Datum.h
* @brief A datum is the default data point for likelihoods, consisting of an input and output type.
* 		 The reliability is measures the reliability of the data (sometimes number of effective data 
* 		 points, sometimes its the noise in the likelihood. 
*/ 
template<typename __input_t, typename __output_t>
class defaultdatum_t { // a single data point
public:
	using input_t  = __input_t;
	using output_t = __output_t;

	static const char DATA_IO_DELIMITER = ':';

	input_t  input;
	output_t output;
	double   reliability; // the noise probability (typically required)
	double   count;
	
	defaultdatum_t() { }
	defaultdatum_t(const input_t& i, const output_t& o, double r=NaN, double c=1.0) : input(i), output(o), reliability(r), count(c) {}
	defaultdatum_t(const std::string s){
		
		// if s is x:y then its input output
		// otherwise, we'll treat it as a thunk, no input
		if(contains(s, DATA_IO_DELIMITER)) {
		
			// define this so we can use string_to
			auto [x, y] = split<2>(s, DATA_IO_DELIMITER);
			input = string_to<input_t>(x);
			output = string_to<output_t>(y);
		}
		else {
			input = input_t{};
			output = string_to<output_t>(s);
		}
		
		reliability = NaN;
		count = 1;
	}
	
	/**
	 * @brief Defined to allow nan in reliability
	 * @param y
	 * @return 
	 */	
	bool operator==(const defaultdatum_t& y) const {
		
		return input==y.input and output==y.output and count == count and
			   (reliability==y.reliability or (isnan(reliability) and isnan(y.reliability)));
	}	
}; 

template<typename input_t, typename output_t>
std::ostream& operator<<(std::ostream& o, const defaultdatum_t<input_t,output_t>& d) {
	
	if constexpr( std::is_pointer<input_t>::value and std::is_pointer<output_t>::value) {
		o << "[DATA: PTR " << *d.input << " -> PTR " << *d.output << " w/ reliability " << d.reliability << " and count " << d.count << "]";
	}
	if constexpr( std::is_pointer<input_t>::value and not std::is_pointer<output_t>::value) {
		o << "[DATA: PTR " << *d.input << " -> " << d.output << " w/ reliability " << d.reliability << " and count " << d.count << "]";
	}
	if constexpr( (not std::is_pointer<input_t>::value) and std::is_pointer<output_t>::value) {
		o << "[DATA: " << d.input << " -> " << *d.output << " w/ reliability " << d.reliability << " and count " << d.count << "]";
	}
	if constexpr( (not std::is_pointer<input_t>::value) and (not std::is_pointer<output_t>::value)) {
		o << "[DATA: " << d.input << " -> " << d.output << " w/ reliability " << d.reliability << " and count " << d.count << "]";
	}

	// TODO: Check if pointer and deref for printing
	return o;
}

template<typename input_t, typename output_t>
std::string str(const defaultdatum_t<input_t, output_t>& x) {
	return str(x.input) + defaultdatum_t<input_t, output_t>::DATA_IO_DELIMITER + str(x.output);
}