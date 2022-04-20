#pragma once 

#include <map>
#include <string>
#include "Singleton.h"
#include "Polynomial.h"
#include "ReservoirSample.h"

/**
 * @class StructuresToSamples
 * @author Steven Piantadosi
 * @date 10/04/22
 * @file StructureToSamples.h
 * @brief A little data structure to store a mapping from structures to posterior samples, using
 * 		  a posterior-weighted reservoir sample. This is mostly a wrapper on a std::map but adds until
 * 	      the number of structures exceeds trim_at, at which point it trims to trim_to by removing the
 * 		  structures with the lowest maximum probability. This lets us store the best performing
 *  	  structures, which are the only ones tht matter in the posterior anyways.
 */

class StructuresToSamples : public Singleton<StructuresToSamples>, 
					        public std::map<std::string,PosteriorWeightedReservoirSample<MyHypothesis>> { 
public:

	const size_t trim_at = 5000; // when we get this big in overall_sample structures
	const size_t trim_to = 1000;  // trim to this size
	
	void add(MyHypothesis& h) {
		
		// convert to a structure string
		auto ss = h.structure_string(false); 
		
		// create it if it doesn't exist
		if(not this->count(ss)) { 
			this->emplace(ss,nsamples);
		}
		
		// and add it
		(*this)[ss] << h;
		
		// and trim if we must
		if(this->size() >= trim_at) {
			trim(trim_to);
		}
	}
	void operator<<(MyHypothesis& h) { add(h);}
	
	double max_posterior(const PosteriorWeightedReservoirSample<MyHypothesis>& rs) {
		return max_of(rs.values(), get_posterior<MyHypothesis>).second;
	}

	void trim(const size_t N) {
		
		if(this->size() < N or N <= 0) 
			return;
		
		std::vector<double> structureScores;
		for(auto& [ss, R] : *this) {
			structureScores.push_back( max_posterior(R) );
		}

		// sorts low to high to find the nstructs best's score
		std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
		double cutoff = structureScores[N-1];
		
		// now go through and trim
		auto it = this->begin();
		while(it != this->end()) {
			double b = max_posterior(it->second);
			if(b < cutoff) { // if we erase
				it = this->erase(it); // wow, erases returns a new iterator which is valid
			}
			else {
				++it;
			}
		}
	}
	
} overall_samples;
