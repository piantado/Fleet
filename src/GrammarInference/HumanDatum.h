#pragma once 

#include <map>

/**
 * @class HumanDatum
 * @author piantado
 * @date 03/08/20
 * @file GrammarHypothesis.h
 * @brief This stores the human data, as a list of data to condition on data[0:ndata], the data you predict on, and then correct/total counts
 */
template<typename HYP>
struct HumanDatum {
	HYP::data_t*  data; 
	size_t        ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	HYP::datum_t* predict;	// what we compute on now
	std::map<typename HYP::output_t,size_t> responses; // how many of each type of response do you see?
	double        chance; // how many responses are alltogether possible? Needed for chance responding. 
	
	// this is used for computing memory decay. For efficiency this works in reverse order, so
	// the first data point should get decay_position=0 and the most recent should have e.g. 
	// decay_position=ndata. If set to zero, we don't decay. 
	//int           decay_position; 
};
