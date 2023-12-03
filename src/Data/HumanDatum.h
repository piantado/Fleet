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
		 typename _output_t=typename HYP::output_t,
		 typename _response_t=std::vector<std::pair<_output_t,size_t>>, 
		 typename _data_t=typename HYP::data_t>
struct HumanDatum {
	using data_t = _data_t;
	using input_t = _input_t;
	using output_t = _output_t;
	using response_t = _response_t;
	
	data_t* const            data; 
	const size_t             ndata; // we condition on the first ndata points in data (so we can point to a single stored data)
	input_t* const           predict;	// what we compute on now
	const response_t         responses; // vector giving how many of each type of response you see
	const double             chance; // how many responses are alltogether possible? Needed for chance responding. 
	std::vector<int>* const  decay_position; // a pointer to memory decay positions for each of ndata (i'th point is decayed (my_decay_position-decay_position[i])**(-decay))
	const int				 decay_index; // what position was I in terms of decay?
};



///**
// * @class HumanDataSeries
// * @author Steven Piantadosi
// * @date 02/12/23
// * @file HumanDatum.h
// * @brief A human data series contains a list of human data points, some of which may be presented at the same "position" (meaning there is no decay between)
// */
//struct HumanDataSeries {
//	std::vector<int> decay_position; 
//	std::vector<HumanDatum> hd;
//	int max_decay_position = 0;
//	
//	
//	void add(HumanDatum& d, int decaypos) {
//		decay_position.push_back(decaypos);
//		hd.push_back(d);
//		max_decay_position = std::max(max_decay_position, decaypos);
//	}
//	
//	/**
//	 * @brief Returns the decay time in computing the likelihood for the n'th data point, using the i'th back
//	 * @param n
//	 * @param i
//	 * @return 
//	 */	
//	size_t decay_t(size_t n, size_t i) {
//		return decay_position.at(n)-decay_position.at(i);
//	}
//
//	size_t size() {
//		return hd.size();
//	}
//	
//}
