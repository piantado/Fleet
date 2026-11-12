
#pragma once 

#include <mutex>
#include "MedianFAME.h"

/**
 * @class StreamingStatistics
 * @author piantado
 * @date 29/01/20
 * @file StreamingStatistics.h
 * @brief A class to store a bunch of statistics about incoming data points, including min, max, mean, etc. 
 */	
class StreamingStatistics {
protected:
	mutable std::mutex lock;		
	
public:

	double min;
	double max;
	
	// for Welford's algorithm
	double mean;
	double M2;
	
	double N; // how many times add() was called, INCLUDING nan/inf values
	
	// How many of those were finite. This (not N) is the sample size for mean and M2: N also counts
	// nan/inf values, and using it in Welford's update made the mean and variance wrong as soon as
	// one of those had been added (e.g. {-inf, -inf, 10, 10} had mean 5 and sd 5.77).
	double Nfinite; 
	
	MedianFAME<double>  streaming_median;
	
	StreamingStatistics(size_t rs=100) : 
		min(infinity), max(-infinity), mean(0.0), M2(0.0), N(0), Nfinite(0) {
	}
	
	StreamingStatistics(const StreamingStatistics& s) {
		std::lock_guard guard(s.lock); // acquire s's lock
		std::lock_guard guard2(lock); // and my own
		min = s.min;
		max = s.max;
		mean = s.mean;
		M2 = s.M2;
		N   = s.N;
		Nfinite = s.Nfinite;
		streaming_median = s.streaming_median;
	}
	
	void operator=(StreamingStatistics&& s) {
		std::lock_guard guard(s.lock); // acquire s's lock
		std::lock_guard guard2(lock); // and my own
		min = s.min;
		max = s.max;
		mean = s.mean;
		M2 = s.M2;
		N   = s.N;
		Nfinite = s.Nfinite;
		streaming_median = s.streaming_median;
	}

	void add(double x) {
		/**
		 * @brief Add sample x to these statistics. 
		 * @param x
		 */
		
		// get the lock (before touching N: this used to increment N, a plain double, outside the lock,
		// which is a data race when several MCTS threads add to the same node)
		std::lock_guard guard(lock);
		
		++N; // always count N, even if we get nan/inf (this is required for MCTS, otherwise we fall into sampling nans)
		
		// filter nans and inf(TODO: Should we filter inf?)
		if(std::isnan(x) or std::isinf(x)) 
			return; 
		
		++Nfinite;

		streaming_median << x;

		max = std::max(max, x);
		min = std::min(min, x);
		
		// now update -- Welford's algorithm
		
		double delta = x - mean;
		mean += delta/Nfinite;
		
		double delta2 = x - mean;
		M2 += delta*delta2;
	}
	
	void operator<<(double x) { 
		/**
		 * @brief Add x
		 * @param x
		 */
		add(x);
	}
	
	double median() const {
		/**
		 * @brief Compute the median according to my reservoir sample. 
		 * @return 
		 */
		return streaming_median.median();
	}
	
	double get_sd() {
		return sqrt(get_variance());
	}
	
	double get_variance() {
		std::lock_guard guard(lock);
		if(Nfinite<2) return NaN;
		else 	      return M2/(Nfinite-1);
	}	
};


