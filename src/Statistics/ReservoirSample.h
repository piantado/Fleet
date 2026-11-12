#pragma once

#include "Errors.h"
#include "TopN.h"

/**
 * @class ReservoirSample
 * @author piantado
 * @date 29/01/20
 * @file ReservoirSample.h
 * @brief A simple resevoir sampling algorithm. One great disappointment is that this doesn't implement
 * 		  the version where you draw a random unifom for each number and store the lowest. That's such
 * 		  a pretty idea. 
 */
template<typename T>
class ReservoirSample : public Serializable<ReservoirSample<T>> {
	
public:

	std::vector<T> samples;
	size_t capacity; 
	unsigned long N; // how many have I seen? Any time I *try* to add something to this, N gets incremented
	
protected:
	//mutable std::mutex lock;		

public:
	ReservoirSample(size_t n=100) : capacity(n), N(0) {	}	

	void set_reservoir_size(const size_t s) {
		/**
		 * @brief How big should the reservoir size be?
		 * @param s
		 */
		samples.reserve(s);
		capacity = s;
	}
	
	size_t size() const {
		/**
		 * @brief How many elements are currently stored?
		 * @return 
		 */
		return samples.size();
	}

	virtual void add(T x) {
		++N;
		
		if(samples.size() < capacity) {
			samples.push_back(x);
		}
		else { 
			auto which = myrandom(N); // NOTE: it's not capacity, it's N!
			if(which < capacity) {
				samples[which] = x;
			}		
		}		
	}
	void operator<<(T x) {	add(x); }
	

	const std::vector<T>& values() const {	return samples; }
	
	void clear() {
		samples.clear();
	}
	
	virtual std::string serialize() const override { throw NotImplementedError(); }
	
	static ReservoirSample<T> deserialize(const std::string&) { throw NotImplementedError(); }
	
};




/**
 * @class PosteriorWeightedReservoirSample
 * @author Steven Piantadosi
 * @date 12/02/22
 * @file ReservoirSample.h
 * @brief Same as ReservoirSample but each item's chance of being in the sample is proportional to 
 * 		  exp(h.posterior) -- Chao's (1982) algorithm: once the reservoir is full, the i'th item replaces a
 * 		  uniformly chosen element with probability capacity * w_i / (w_1 + ... + w_i).
 * 		  NOTE: when a single item is "overweight" (capacity * w_i exceeds the total weight so far) that 
 * 		  probability is capped at 1, so its inclusion probability is less than proportional; this is the
 * 		  known limitation of the simple form of the algorithm. 
 * 		  NOTE that with this it is likely important to use some burn-in because otherwise it will start off
 *        full of crummy samples. 
 */
template<typename T>
class PosteriorWeightedReservoirSample : public Serializable<PosteriorWeightedReservoirSample<T>> {
	
public:

	std::vector<T> samples;
	size_t capacity; // how many should I have?
	unsigned long N; // how many have I seen? Any time I *try* to add something to this, N gets incremented
	double weight_lse; 

	PosteriorWeightedReservoirSample(size_t s=100) : capacity(s), N(0), weight_lse(-infinity) {	}	

	void set_reservoir_size(const size_t s) {
		/**
		 * @brief How big should the reservoir size be?
		 * @param s
		 */
		capacity = s;
		samples.reserve(s);
	}
	
	size_t size() const {
		/**
		 * @brief How many elements are currently stored?
		 * @return 
		 */
		return samples.size();
	}

	virtual void add(T x) {
		++N;
		
		if(std::isnan(x.posterior) or x.posterior == -infinity)
			return;
		
		// NOTE: logplusexp_full, not logplusexp: the approximate version drops any term more than ~13.8 
		// below the running total, so after ~1e6 comparable items the total would stop growing. 
		weight_lse = logplusexp_full(weight_lse, x.posterior);
		
		//https://en.wikipedia.org/wiki/Reservoir_sampling#Weighted_random_sampling
		
		if(capacity == 0) return; 
		
		if(samples.size() < capacity) {
			samples.push_back(x);
		}
		else {
			// NOTE: the factor of capacity is required: without it (as this used to be) an item late in the
			// stream was included with probability w/W instead of capacity*w/W, so with equal weights, 
			// N=1000 and capacity=10 the first item was kept 64% of the time and the last 0.1% (both
			// should be 1%). uniform() < p is false for p=NaN and always true for p>=1. 
			const double p = (double)capacity * exp(x.posterior - weight_lse);
			if(uniform() < p) {
				auto which = myrandom(capacity);
				samples[which] = x;
			}			
		}
	}
	void operator<<(T x) {	add(x); }
	
	/**
	 * @brief Get a multiset of values (ignoring the reservoir weights)
	 * @return 
	 */			
	const std::vector<T>& values() const { return samples; }
	void clear() {	samples.clear(); }
	
	virtual std::string serialize() const override { throw NotImplementedError(); }
	static PosteriorWeightedReservoirSample<T> deserialize(const std::string&) { throw NotImplementedError(); }
	
};

