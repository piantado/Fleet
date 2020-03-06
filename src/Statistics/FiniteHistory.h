#pragma once 

#include <mutex>

namespace Fleet {
	namespace Statistics {
		

		/**
		 * @class FiniteHistory
		 * @author steven piantadosi
		 * @date 03/02/20
		 * @file FiniteHistory.h
		 * @brief A FiniteHistory stores the previous N examples of something of type T. This is used e.g. in 
		 * 		  MCMC in order to count the acceptance ratio on the previous N samples. 
		 */		
		template<typename T>
		class FiniteHistory {
			// store a finite set of previous samples of T
			// allowing us to compute prior means, etc
		public:
			std::vector<T> history;
			std::atomic<size_t> history_size;
			std::atomic<size_t> history_index;
			std::atomic<unsigned long> N;
			mutable std::mutex mutex;
			
			FiniteHistory(size_t n) : history_size(n), history_index(0), N(0) {
				
			}
			
			// default size
			FiniteHistory() : history_size(100), history_index(0), N(0) { 
			}
			
			FiniteHistory(const FiniteHistory &fh) {
				history = fh.history;
				history_size = (size_t)fh.history_size;
				history_index = (size_t)fh.history_index;
				N = (unsigned long)fh.N;
			}
			
			FiniteHistory(FiniteHistory&& fh) {
				history = fh.history;
				history_size = (size_t)fh.history_size;
				history_index = (size_t)fh.history_index;
				N = (unsigned long)fh.N;
			}
			
			
			void operator=(const FiniteHistory &fh) {
				history = fh.history;
				history_size = (size_t)fh.history_size;
				history_index = (size_t)fh.history_index;
				N = (unsigned long)fh.N;
			}
			
			void operator=(FiniteHistory&& fh) {
				history = std::move(fh.history);
				history_size = (size_t)fh.history_size;
				history_index = (size_t)fh.history_index;
				N = (unsigned long)fh.N;
			}
			
			
			
			/**
			 * @brief Add x to this history
			 * @param x
			 */			
			void add(T x) {
				std::lock_guard guard(mutex);
				++N;
				if(history.size() < history_size){
					history.push_back(x);
					history_index++;
				}
				else {
					history[history_index++ % history_size] = x;
				}
			}

			/**
			 * @brief Convenient adding
			 * @param x
			 */
			void operator<<(T x) { add(x); }
			
			/**
			 * @brief Compute the average
			 * @return 
			 */			
			double mean() {
				std::lock_guard guard(mutex);
				double sm=0;
				for(auto a: history) sm += a;
				return double(sm) / history.size();
			}
			
		};
		
	} // end namespace
}
