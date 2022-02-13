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
	ReservoirSample(size_t n=100) : N(0), capacity(n) {	}	

	void set_reservoir_size(const size_t s) const {
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
			if(uniform()*N < 1.0) {
				auto which = myrandom(capacity);
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
 * @brief Same as ReservoirSample but chooses proportional to h.posterior. 
 */

template<typename T>
class PosteriorWeightedReservoirSample : public Serializable<PosteriorWeightedReservoirSample<T>> {
	
public:

	std::vector<T> samples;
	size_t capacity; // how many should I have?
	unsigned long N; // how many have I seen? Any time I *try* to add something to this, N gets incremented
	double weight_lse; 

	PosteriorWeightedReservoirSample(size_t s=100) : capacity(s), N(0), weight_lse(-infinity) {	}	

	void set_reservoir_size(const size_t s) const {
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
		
		weight_lse = logplusexp(weight_lse, x.posterior);
		
		//https://en.wikipedia.org/wiki/Reservoir_sampling#Weighted_random_sampling
		
		if(samples.size() < capacity) {
			samples.push_back(x);
		}
		else {
			if(uniform() < exp(x.posterior - weight_lse)) {
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

