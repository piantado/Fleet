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
template<typename HYP, typename _output_t=HYP::output_t, typename _datum_t=HYP::datum_t, typename _data_t=HYP::data_t>
struct HumanDatum {
	typedef _data_t   data_t;
	typedef _datum_t  datum_t;
	typedef _output_t output_t;
	
	data_t*                   data; 
	size_t                    ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	HYP::input_t*             predict;	// what we compute on now
	std::map<output_t,size_t> responses; // how many of each type of response do you see?
	double                    chance; // how many responses are alltogether possible? Needed for chance responding. 
	int                       decay_position; // for memory decay that's decay_position**(-decay)
};
