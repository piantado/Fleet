#pragma once 

#include <vector>
#include <map>

// Sometimes it is helpful to have the data stored here
// in these guys instead of having to use Hdata_t which
// is typically a span (s non-owning)
template<typename HYP, 
		 typename _ConditionOn_t=HYP::data_t,
		 typename _Response_t=std::map<typename HYP::output_t,int>>
struct HumanDatum {
	using ConditionOn_t = _ConditionOn_t;
	using Hinput_t = typename HYP::input_t;
	using Response_t = _Response_t;
	
	HumanDatum(ConditionOn_t& c,
			   std::vector<float> di,
			   Hinput_t gen,
			   Response_t hr) :
			condition_on(c),
			decay_indices(di),
			generalize(gen),
			human_responses(hr)
	{
	
	}

	// What data did the human see?
	// NOTE that this will typically be a span (defaulty for LOTHypothesis)
	ConditionOn_t condition_on;
	
	// one for each of condition_on,  how far "back" in time is each 
	// (for decay)
	std::vector<float> decay_indices; // each condition_on has some decay_position

	// what they are asked to respond on 
	// NOTE this is a single input point, not a sequence
	Hinput_t generalize; 

	// the map of counts for each human response
	Response_t human_responses;

};

//
//
//template<typename... Args>
//std::ostream& operator<<(std::ostream& o, const HumanDatum<Args...>& d) {
//	
//	// TODO: Check if pointer and deref for printing
//	return o;
//}
