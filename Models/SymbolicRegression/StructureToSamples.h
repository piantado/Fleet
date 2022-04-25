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
 * @brief A little data structure to store a mapping from structures and approx posteriors to posterior samples, using
 * 		  a posterior-weighted reservoir sample. This is mostly a wrapper on a std::map but adds until
 * 	      the number of structures exceeds trim_at, at which point it trims to trim_to by removing the
 * 		  structures with the lowest maximum probability. This lets us store the best performing
 *  	  structures, which are the only ones tht matter in the posterior anyways.
 * 
 * 		  NOTE: We learned that we need to map from approx posteriors not just structures since many
 *			of the structures are multimodal, so that if we only do it by structure, we end up with 
 * 			bad modes for a structure overwriting the good ones. 
 */

class StructuresToSamples : public Singleton<StructuresToSamples> { 
public:
	using key_type   = std::pair<std::string,double>;
	using value_type = ReservoirSample<MyHypothesis>;
	
	std::map<key_type,value_type> M;
	
	const size_t trim_at = 5000; // when we get this big in overall_sample structures
	const size_t trim_to = 1000;  // trim to this size
	
	const double rounding_factor = 2.0; // round to the nearest this for determining groups
	
	key_type key(MyHypothesis& h) {
		return std::make_pair(h.structure_string(false), 
					          round(h.posterior/rounding_factor)*rounding_factor );
	}
	
	// Some light wrappers for M
	decltype(M.begin()) begin() { return M.begin(); }
	decltype(M.end())   end() { return M.end(); }	
	size_t size() const { return M.size(); }	
	value_type& operator[](key_type& t) { return M[t]; }
	
	
	void add(MyHypothesis& h) {
		
		// convert to a key for the map
		auto k = key(h); 
	
		// create it if it doesn't exist
		if(not M.count(k)) { 
			M.emplace(k,nsamples);
		}
		
		// and add it
		M[k] << h;
		
		// and trim if we must
		if(M.size() >= trim_at) {
			trim(trim_to);
		}
	}
	void operator<<(MyHypothesis& h) { add(h);}
	
	double max_posterior(const ReservoirSample<MyHypothesis>& rs) {
		return max_of(rs.values(), get_posterior<MyHypothesis>).second;
	}

	void trim(const size_t N) {
		
		if(M.size() < N or N <= 0) 
			return;
		
		std::vector<double> structureScores;
		for(auto& [ss, R] : M) {
			structureScores.push_back( max_posterior(R) );
		}

		// sorts low to high to find the nstructs best's score
		std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
		double cutoff = structureScores[N-1];
		
		// now go through and trim
		auto it = M.begin();
		while(it != M.end()) {
			double b = max_posterior(it->second);
			if(b < cutoff) { // if we erase
				it = M.erase(it); // wow, erases returns a new iterator which is valid
			}
			else {
				++it;
			}
		}
	}
	
} overall_samples;
