#pragma once 


namespace Fleet {
	namespace Statistics {
			

		/**
		 * @class MedianFAME
		 * @author steven piantadosi
		 * @date 03/02/20
		 * @file MedianFAME.h
		 * @brief A streaming median class implementing the FAME algorithm
		 * 			Here, we initialize both the step size and M with the current sample
		 * 			http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.108.7376&rep=rep1&type=pdf
		 */
		template<typename T>
		class MedianFAME {
		public:

			T M;
			size_t n;
			T step;
			
			MedianFAME() : M(0.0), n(0), step(NaN) {
			}	
				
			T median() const { 
				if(n == 0) 
					return NaN;
				else
					return M;
			}
			
			void add(T x) {
				
				if(n == 0) {
					M = x;
					step = sqrt(abs(x)); // seems bad to initialize at x b/c then we start with giant sign swings; sqrt has a nice aesthetic
				}
				else {
					
					if(x > M) {
						M += step;
					}
					else if(x <= M) {
						M -= step;
					}
					
					if(abs(x-M) < step) {
						step = step/2.0;
					}
				}
				++n;
			}
			void operator<<(T x) {	add(x);}

			
		};


	}
}