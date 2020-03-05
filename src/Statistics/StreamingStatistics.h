
#pragma once 

#include <mutex>
#include "MedianFAME.h"
#include "ReservoirSample.h"

namespace Fleet {
	namespace Statistics {
				
		/**
		 * @class StreamingStatistics
		 * @author piantado
		 * @date 29/01/20
		 * @file StreamingStatistics.h
		 * @brief A class to store a bunch of statistics about incoming data points, including min, max, mean, etc. 
		 * 		  This also stores a reservoir sample and allow us to compute how often one distribution exceeds another
		 */	
		class StreamingStatistics {
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
			
			StreamingStatistics(const StreamingStatistics& s) {
				min = s.min;
				max = s.max;
				sum = s.sum;
				lse = s.lse;
				N   = s.N;
				streaming_median = s.streaming_median;
				reservoir_sample = s.reservoir_sample;
			}
			
			void operator=(StreamingStatistics&& s) {
				std::lock_guard guard(s.lock); // acquire s's lock
				min = s.min;
				max = s.max;
				sum = s.sum;
				lse = s.lse;
				N   = s.N;
				streaming_median = s.streaming_median;
				reservoir_sample = s.reservoir_sample;
			}

			void add(double x) {
				/**
				 * @brief Add sample x to these statistics. 
				 * @param x
				 */
				
				++N; // always count N, even if we get nan/inf (this is required for MCTS, otherwise we fall into sampling nans)
				if(std::isnan(x) or std::isinf(x)) return; // filter nans and inf(TODO: Should we filter inf?)
				
				std::lock_guard guard(lock);

				streaming_median << x;
				reservoir_sample << x;
				
				if(x < min) min = x;
				if(x > max) max = x;
				
				if(! std::isinf(x) ) { // we'll filter zeros from this
					sum += x;
					lse = logplusexp(lse, x);
				}
			}
			
			void operator<<(double x) { 
				/**
				 * @brief Add x
				 * @param x
				 */
						
				add(x);
			}
			
			double sample() const {
				/**
				 * @brief Treat as a reservoir sample to sample an element. 
				 * @return 
				 */
				
				return reservoir_sample.sample();
			}
			
			double median() const {
				/**
				 * @brief Compute the median according to my reservoir sample. 
				 * @return 
				 */
				return streaming_median.median();
			}
			
			void print() const {
				for(auto& a : reservoir_sample.top.values()){
					std::cout << a.x << "[" << a.r << "]" << std::endl;
				}
			}
			
			double p_exceeds_median(const StreamingStatistics &q) const {
				/**
				 * @brief What proportion of my samples exceed the median of q?
				 * @param q
				 * @return 
				 */
				size_t k = 0;
				double qm = q.median();
				for(auto& a : reservoir_sample.top.values()) {
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
	}
}

