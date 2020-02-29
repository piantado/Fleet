#pragma once

#include "Top.h"

namespace Fleet {
	namespace Statistics {
				

		/**
		 * @class ReservoirSample
		 * @author piantado
		 * @date 29/01/20
		 * @file ReservoirSample.h
		 * @brief  A special weighted reservoir sampling class that allows for logarithmic weights
		 * 	       to do this, we use a transformation following https://en.wikipedia.org/wiki/Reservoir_sampling#Weighted_random_sampling_using_Reservoir
		 * 		   basically, we want to give a weight that is r^1/w, 
		 * 	 	   or log(r)/w, or log(log(r))-log(w). But the problem is that log(r) is negative so log(log(r)) is not defined. 
		 * 	 	   Instead, we'll use the function f(x)=-log(-log(x)), which is monotonic. So then,
		 * 		   -log(-log(r^1/w)) = -log(-log(r)/w) = -log(-log(r)*1/w) = -[log(-log(r)) - log(w)] = -log(-log(r)) + log(w).
		 * 
		 * 		   NOTE: Because we use TopN internally, we can access the Item values with ReservoirSample.top.values() and we can get
		 *               the stuff that's stored with ReservoirSample.values()
		 */
		template<typename T>
		class ReservoirSample {
			
		public:


			/**
			 * @class Item
			 * @author piantado
			 * @date 29/01/20
			 * @file ReservoirSample.h
			 * @brief An item in a reservoir sample, grouping together an element x and its log weights, value, etc. 
			 */
			class Item {
			public:
				T x;
				const double r; 
				const double lw; // the log weight -- passed in in log form (as in a posterior probability)
				const double lv; // my value -- a function of my r and w.
				
				Item(T x_, double r_, double lw_=0.0) : x(x_), r(r_), lw(lw_), 
					lv(-log(-log(r)) + lw_) {}
				
				bool operator<(const Item& b) const {
					return lv < b.lv;
				}
				
				bool operator==(const Item& b) const {
					// equality here checks r and lw (which determine lv)
					return x==b.x && r==b.r && lw==b.lw;
				}
				
				void print() const {
					assert(0); // not needed here but must be defined to use in TopN
				}
			};
			
		public:

			Fleet::Statistics::TopN<Item> top;
			unsigned long N; // how many have I seen? Any time I *try* to add something to this, N gets incremented
			
		protected:
			//mutable std::mutex lock;		

		public:

			ReservoirSample(size_t n) : N(0){
				top.set_size(n);
			}	
			ReservoirSample() : N(0) {
			}
			
			void set_reservoir_size(const size_t s) const {
				/**
				 * @brief How big should the reservoir size be?
				 * @param s
				 */
				top.set_size(s);
			}
			
			size_t size() const {
				/**
				 * @brief How many elements are currently stored?
				 * @return 
				 */
				return top.size();
			}

			void add(T x, double lw=0.0) {
				//std::lock_guard guard(lock);
				top << Item(x, uniform(), lw);
				++N;				
			}
			void operator<<(T x) {	add(x); }
			
			
			/**
			 * @brief Get a multiset of values (ignoring the reservoir weights)
			 * @return 
			 */			
			std::vector<T> values() const {
				
				std::vector<T> out;
				for(auto& i : top.values()){
					out.push_back(i.x);
				}
				return out;
			}
			
			T sample() const {
				/**
				 * @brief Return a sample from my vals (e.g. a sample of the samples I happen to have saved)
				 * @return 
				 */
				
				if(N == 0) return NaN;
				
				
				double lz = -infinity;
				for(auto& i : top.values()) {
					lz = logplusexp(lz, i.lw); // the log normalizer
				}
				
				double zz = -infinity;
				double r = log(uniform()) + lz; // random number -- uniform*z 
				for(auto& i : top.values()) {
					zz = logplusexp(zz, i.lw); // NOTE: This may be slightly imperfect with our approximations to lse, but it should not matter much
					if(zz >= r) return i.x;			
				}
				assert(0 && "*** Should not get here");
			}
			
			void clear() {
				top.clear();
			}
			
		};

	}
}