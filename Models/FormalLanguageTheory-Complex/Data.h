#ifndef DATA_H
#define DATA_H

#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include "DiscreteDistribution.h"


template<typename tdata>
void load_data_file(std::vector<tdata> &data, const char* datapath) {
	/**
	 * @brief Load data from a file and puts stringprobs into obs, which is a dictionary of strings to ndata total elements, and obsv, which is a vector that is sorted by probability
	 * @param data
	 * @param datapath
	 */
	
	FILE* fp = fopen(datapath, "r");
	if(fp==NULL) { fprintf(stderr, "*** ERROR: Cannot open file! [%s]", datapath); exit(1);}
	
	char* line = NULL; size_t len=0; 
        
	char buffer[100000];
    
	double cnt;
	while( getline(&line, &len, fp) != -1 ) {
		if( line[0] == '#' ) continue;  // skip comments
		else if (sscanf(line, "%s\t%lf\n", buffer, &cnt) == 2) { // floats
			data.push_back(tdata({std::string(""), std::string(buffer), cnt}) );
		}
		else {
			fprintf(stderr, "*** ERROR IN PARSING INPUT [%s]\n", line);
			exit(1);
		}
	}
	fclose(fp);
	
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TODO: Put all this into another library

template<typename T, typename TDATA>
std::map<T, double> highest(const std::vector<TDATA>& m, unsigned long N) {
	// take a type of data and make a map of strings to reliability, pulling out the top N
	// and converting into a map
	std::map<T, double> out;
	
	std::vector<TDATA> v = m; 
	std::sort(v.begin(), v.end(), [](auto x, auto y){ return x.reliability > y.reliability; });
	
	for(size_t i=0;i<std::min(N, v.size()); i++) {
		out[v[i].output] = v[i].reliability; // remember: must be output since that's what we're modeling
	}
	return out;
}



template<typename TDATA>
std::pair<double, double> get_precision_and_recall(std::ostream& output, DiscreteDistribution<std::string>& model, std::vector<TDATA>& data, unsigned long N) {
	// How many of the top N generated strings appear *anywhere* in the data
	// And how many of the top N data appear *anywhere* in the generated strings
	// Note: This is a little complicated if the data has fewer strings that the model, since we don't
	// know the "true" precision and recall
	
	auto A = model.best(N);
	auto B = highest<std::string,TDATA>(data,   std::min(N, data.size())  );
	
	std::set<std::string> mdata; // make a map of all observed output strings
	for(auto v : data) mdata.insert(v.output); 
	
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
	
	// and compute the max probability difference
	// between items of data and their 
//	unsigned long z = 0; // get normalizing constant for data
//	for(auto d : data) { z += d.reliability; }
//	
//	double maxpdiff = -infinity;
//	for(auto a : model.values()) {
//		double dp = 0;
//		if(mdata)
//		double d = exp(model.second) - 
//	}
	
	return std::make_pair(double(nprec)/A.size(), double(nrec)/B.size());
}




#endif