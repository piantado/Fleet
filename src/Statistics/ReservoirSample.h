#pragma once

#include "Top.h"

namespace Fleet {
	namespace Statistics {
				

		/**
		 * @class ReservoirSample
		 * @author piantado
		 * @date 29/01/20
		 * @file ReservoirSample.h
		 * @brief A reservoir sampling algorithm. 
		 * 		  NOTE: This was simplified from an old version that permitted unequal weights among elements. We may go back to that eventually - https://en.wikipedia.org/wiki/Reservoir_sampling#Weighted_random_sampling_
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
				
				Item(T x_, double r_) : x(x_), r(r_) {}
				
				bool operator<(const Item& b) const {
					return r < b.r;
				}
				
				bool operator==(const Item& b) const {
					// equality here checks r and lw (which determine lv)
					return x==b.x && r==b.r;
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

			void add(T x) {
				//std::lock_guard guard(lock);
				top << Item(x, uniform());
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
				 * @brief Return a sample from my vals
				 * @return 
				 */
				if(N == 0) return NaN;

				auto it = top.s.begin();
				
				std::advance(it, myrandom(top.size()));
				
				return it->x;
			}
			
			void clear() {
				top.clear();
			}
			
		};

	}
}