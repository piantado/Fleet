#pragma once

#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include "DiscreteDistribution.h"

#include "FleetArgs.h"

template<typename datum_t>
void load_data_file(std::vector<datum_t> &data, const char* datapath) {
	/**
	 * @brief Load data from a file and puts stringprobs into obs, which is a dictionary of strings to ndata total elements, and obsv, which is a vector that is sorted by probability
	 * @param data
	 * @param datapath
	 */
	
	for(auto [s, cnt] : read_csv<2>(datapath, false, '\t')) {
		data.emplace_back(std::string(""), s, NaN, std::stod(cnt));
	}	
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TODO: Put all this into another library

template<typename T, typename TDATA>
std::map<T, double> highest(const std::vector<TDATA>& m, unsigned long N) {
	// take a type of data and make a map of strings to counts, pulling out the top N
	// and converting into a map

	std::map<T, double> out;
	
	std::vector<TDATA> v = m; 
	std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.count > y.count; });
	
	for(size_t i=0;i<std::min(N, v.size()); i++) {
		out[v[i].output] = v[i].count; // remember: must be output since that's what we're modeling
	}
	return out;
}



template<typename TDATA>
std::pair<double, double> get_precision_and_recall(DiscreteDistribution<std::string>& model, std::span<TDATA>& data, unsigned long N) {
	// How many of the top N generated strings appear *anywhere* in the data
	// And how many of the top N data appear *anywhere* in the generated strings
	// Note: This is a little complicated if the data has fewer strings that the model, since we don't
	// know the "true" precision and recall
	
	auto A = model.best(N, true);
	auto B = highest<std::string,TDATA>(data, std::min(N,data.size()) );
	
	std::set<std::string> mdata; // make a map of all observed output strings
	for(auto v : data) 
		mdata.insert(v.output); 
	
	unsigned long nprec = 0;
	for(auto a: A) {
		if(mdata.count(a)) 
			nprec++;
	}
	
	unsigned long nrec  = 0;
	for(auto b : B){
		if(model.count(b.first))
			nrec++;
	}
	
	return std::make_pair(double(nprec)/A.size(), double(nrec)/B.size());
}


