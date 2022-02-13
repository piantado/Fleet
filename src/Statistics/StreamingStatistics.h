
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
	
	double N;
	
	MedianFAME<double>  streaming_median;
	
	StreamingStatistics(size_t rs=100) : 
		min(infinity), max(-infinity), mean(0.0), M2(0.0), N(0) {
	}
	
	StreamingStatistics(const StreamingStatistics& s) {
		std::lock_guard guard(s.lock); // acquire s's lock
		std::lock_guard guard2(lock); // and my own
		min = s.min;
		max = s.max;
		mean = s.mean;
		M2 = s.M2;
		N   = s.N;
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
		streaming_median = s.streaming_median;
	}

	void add(double x) {
		/**
		 * @brief Add sample x to these statistics. 
		 * @param x
		 */
		
		++N; // always count N, even if we get nan/inf (this is required for MCTS, otherwise we fall into sampling nans)
		
		// filter nans and inf(TODO: Should we filter inf?)
		if(std::isnan(x) or std::isinf(x)) 
			return; 
		
		// get the lock
		std::lock_guard guard(lock);

		streaming_median << x;

		max = std::max(max, x);
		min = std::min(min, x);
		
		// now update -- Welford's algorithm
		
		double delta = x - mean;
		mean += delta/N;
		
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
		if(N<2) return NaN;
		else 	return M2/(N-1);
	}	
};


