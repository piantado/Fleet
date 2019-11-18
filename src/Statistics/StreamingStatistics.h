
#pragma once 

#include <mutex>
#include "MedianFAME.h"
#include "ReservoirSample.h"

class StreamingStatistics {
	// a class to store a bunch of statistics about incoming data points, including min, max, mean, etc. 
	// also stores a reservoir sample and allow us to compute how often one distribution exceeds another
	
protected:
	mutable std::mutex lock;		
	
public:

	double min;
	double max;
	double sum;
	double lse; // logsumexp
	double N;
	
	MedianFAME<double>      streaming_median;
	ReservoirSample<double> reservoir_sample;
	
	StreamingStatistics(size_t rs=1000) : 
		min(infinity), max(-infinity), sum(0.0), lse(-infinity), N(0), reservoir_sample(rs) {
	}
	
	void operator=(StreamingStatistics&& s) {
		std::lock_guard guard(s.lock); // acquire s's lock
		min = s.min;
		max = s.max;
		sum = s.sum;
		lse = s.lse;
		N   = s.N;
		streaming_median = std::move(s.streaming_median);
		reservoir_sample = std::move(s.reservoir_sample);
	}

	void add(double x, double lw=0.0) {
		++N; // always count N, even if we get nan/inf (this is required for MCTS, otherwise we fall into sampling nans)
		if(std::isnan(x) || std::isinf(x)) return; // filter nans and inf(TODO: Should we filter inf?)
		
		std::lock_guard guard(lock);

		streaming_median << x;
		reservoir_sample.add(x,lw);
		
		if(x < min) min = x;
		if(x > max) max = x;
		
		if(! std::isinf(x) ) { // we'll filter zeros from this
			sum += x;
			lse = logplusexp(lse, x);
		}
	}
	
	void operator<<(double x) { 
		add(x);
	}
	
	double sample() const {
		return reservoir_sample.sample();
	}
	
	double median() const {
		
		if(reservoir_sample.size() == 0) return -infinity;
		else {
			std::lock_guard guard(lock);
			auto pos = reservoir_sample.vals.begin();
			std::advance(pos,reservoir_sample.size()/2);
			return *pos;
		}
		//return streaming_median.median();
	}
	
	void print() const {
		for(auto a : reservoir_sample.s){
			std::cout << a.x << "[" << a.r << "]" << std::endl;
		}
	}
	
	double p_exceeds_median(const StreamingStatistics &q) const {
		// how often do I exceed the median of q?
		size_t k = 0;
		double qm = q.median();
		for(auto a : reservoir_sample.s) {
			if(a.x > qm) k++;
		}
		return double(k)/reservoir_sample.size();
	}
	
//	double p_exceeds(const StreamingStatistics &q) const {
//		//https://stats.stackexchange.com/questions/71531/probability-that-a-draw-from-one-sample-is-greater-than-a-draw-from-a-another-sa
//		
//		size_t sum = 0;
//		
//		size_t i=0; // where am I in q?
//		for(auto a: q.sample.vals) {
//			auto pos = sample.vals.lower_bound(a); // where would a have gone?
//			size_t d = std::distance(sample.vals.begin(), pos); // NOTE: NOT EFFICIENT
//			size_t rank = d + i; // my overall rank
//			
//			sum += 
//			i++;
//		}
//	}
	
};

