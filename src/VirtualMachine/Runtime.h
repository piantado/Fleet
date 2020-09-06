#pragma once

#include "RuntimeCounter.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Just some functions to help with runtime calculations. Note that these re-run the data,
// so if you are otherwise computing the likelihood, this would be inefficient. 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


template<typename HYP>
RuntimeCounter runtime(HYP& h, HYP::datum_t& di) {
	return callOne_vms(di.input).runtime_counter;
}

template<typename HYP>
RuntimeCounter runtime(HYP& h, HYP::data_t& d) {
	RuntimeCounter rt;
	for(auto& di: d) {
		rt.increment(runtime(h,di));
	}
	return rt;	
}