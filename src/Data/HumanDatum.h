#pragma once 

#include <map>

/**
 * @class HumanDatum
 * @author piantado
 * @date 03/08/20
 * @file GrammarHypothesis.h
 * @brief This stores the human data, as a list of data to condition on data[0:ndata], the data you predict on, and then correct/total counts
 * 			We defaultly use the HYP's output, datum, and data types, but this allows us to specify something different
 * 			if that's more convenient
 */
template<typename HYP, 
		 typename _input_t=typename HYP::input_t, 
		 typename _response_t=std::map<typename HYP::output_t,size_t>, 
		 typename _data_t=typename HYP::data_t>
struct HumanDatum {
	using data_t = _data_t;
	using input_t = _input_t;
	using response_t = _response_t;
	
	data_t* const            data; 
	const size_t             ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	input_t* const           predict;	// what we compute on now
	const response_t         responses; // map giving how many of each type of response you see
	const double             chance; // how many responses are alltogether possible? Needed for chance responding. 
	std::vector<int>* const  decay_position; // a pointer to memory decay positions for each of ndata (i'th point is decayed (my_decay_position-decay_position[i])**(-decay))
	const int				 my_decay_position; // what position was I in terms of decay?
};



