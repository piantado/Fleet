
#pragma once

#include "stdlib.h"
#include "stdio.h"

#include <cmath>
#include <set>
#include <queue>
#include <list>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

namespace Fleet {
	namespace Statistics {
		/**
		 * @class TopN
		 * @author steven piantadosi
		 * @date 03/02/20
		 * @file Top.h
		 * @brief A TopN is a n object you can "add" hypotheses to (via add or <<) and it stores the best N of them.
		 * 			This is widely used in Fleet in order to find good approximations to the top hypotheses found in MCTS or MCMC.
		 * */
		template<class T>
		class TopN {	
			std::mutex lock;
			
		public:
			std::map<T,unsigned long> cnt; // also let's count how many times we've seen each for easy debugging
			std::multiset<T> s; // important that it stores in sorted order by posterior! Multiset because we may have multiple samples that are "equal" (as in SymbolicRegression)
			bool print_best; // should we print the best?
			
			
		public:
			std::atomic<size_t> N;
			
			TopN(size_t n=std::numeric_limits<size_t>::max()) :  print_best(false), N(n) {}
			
			TopN(const TopN<T>& x) {
				clear();
				set_size(x.N);
				add(x);
				print_best = x.print_best;
			}
			TopN(TopN<T>&& x) {
				cnt = x.cnt;
				set_size(x.N);
				s = x.s;
				print_best = x.print_best;
			}
			
			void operator=(const TopN<T>& x) {
				clear();
				set_size(x.N);
				add(x);
				print_best = x.print_best;
			}
			void operator=(TopN<T>&& x) {
				set_size(x.N);
				cnt = x.cnt;
				s =  x.s;
				print_best = x.print_best;
			}
			

			void set_size(size_t n) {
				/**
				 * @brief Set the size of n that I cna have. NOTE: this does not resize the existing data. 
				 * @param n
				 */
				N = n;
			}
			void set_print_best(bool b) {
				/**
				 * @brief As I add things, should I print the best I've seen so far? 
				 * @param b
				 */
				
				print_best = b;
			}

			size_t size() const {
				/**
				 * @brief How many are currently in the set? (NOT the total number allowed)
				 * @return 
				 */
				
				return s.size();
			}
			bool empty() const {
				/**
				 * @brief Is it empty?
				 * @return 
				 */
				
				return s.size() == 0;
			}
			
			const std::multiset<T>& values() const {
				/**
				 * @brief Return a multiset of all the values in TopN
				 * @return 
				 */
				
				return s;
			}

			void add(const T& x, size_t count=1) { 
				/**
				 * @brief Add x. If count is set, that will add that many counts.
				 * 		  NOTE that we do not add objects x such that x.posterior == -infinity or NaN
				 * @param x
				 * @param count
				 */
				
				if(N == 0) return;
				
				// enable this when we use posterior as the variable, we don't add -inf values
				if constexpr (HasPosterior<T>::value) {			
					if(std::isnan(x.posterior) or x.posterior == -infinity) return;
				}
				
				std::lock_guard guard(lock);

				// if we aren't in there and our posterior is better than the worst
				if(s.find(x) == s.end()) {
					
					if(s.size() < N or (empty() or worst() < x )) { // skip adding if its the worst -- can't call worst_score due to lock
						T xcpy = x;
						
						if(print_best and (empty() or best() < x ))  {
							xcpy.print();
						}
					
						s.insert(xcpy); // add this one
						cnt[xcpy] = count;
						
						// and remove until we are the right size
						while(s.size() > N) {
							size_t n = cnt.erase(*s.begin());
							assert(n==1);
							s.erase(s.begin()); 
						}
					}
					else { 			
						// it's not in there so don't increment the count
					}
				} 
				else { // it's in there already
					cnt[x] += count;
				} 
				
			}
			void operator<<(const T& x) {
				/**
				 * @brief Friendlier syntax for adding.
				 * @param x
				 */
				
				add(x);
			}
			
			void add(const TopN<T>& x) { // add from a whole other topN
				/**
				 * @brief Add everything in x to this one. 
				 * @param x
				 */
			
				for(auto& h: x.s){
					add(h, x.cnt.at(h));
				}
			}
			void operator<<(const TopN<T>& x) {
				add(x);
			}
			
			// We also define this so we can pass TopN as a callback to MCMC sampling
			//void operator()(const T& x) { add(x); }
			void operator()(T& x)       { add(x); }
			
			
			// get the count
			size_t operator[](const T& x) {
				/**
				 * @brief Access the counts for a given element x
				 * @param x
				 * @return 
				 */
				
				return cnt[x];
			}
			
			const T& best() { 
				/**
				 * @brief Returns a reference to the best element currently stored
				 * @return 
				 */
				
				assert( (!s.empty()) && "You tried to get the max from a TopN that was empty");
				return *s.rbegin();  
			}
			const T& worst() { 
				/**
				 * @brief Returns a reference to the worst element currently stored
				 * @return 
				 */
				
				assert( (!s.empty()) && "You tried to get the min from a TopN that was empty");
				return *s.begin(); 
			}
			
			double Z() { // compute the normalizer
				/**
				 * @brief Compute the logsumexp of all of the elements stored. 
				 * @return 
				 */
			
				double z = -infinity;
				std::lock_guard guard(lock);
				for(const auto& x : s) 
					z = logplusexp(z, x.posterior);
				return z;       
			}
			
			void print(std::string prefix="") {
				/**
				 * @brief Sort and print from worst to best
				 * @param prefix - an optional prefix to print before each line
				 */
				
				// this sorts so that the highest probability is last
				std::vector<T> v(s.size());
				std::copy(s.begin(), s.end(), v.begin());
				std::sort(v.begin(), v.end()); 
				
				for(auto& h : v) {
					h.print(prefix);
				}
			}
			
			void clear() {
				/**
				 * @brief Remove everything 
				 */
				
				std::lock_guard guard(lock);
				s.erase(s.begin(), s.end());
				cnt.clear();
			}
			
			unsigned long count(const T x) {
				/**
				 * @brief How many times have we seen x?
				 * @param x
				 * @return 
				 */
				
				std::lock_guard guard(lock);
				// This mightt get called by something not in here, so we can't assume x is in 
				if(cnt.count(x)) return cnt.at(x);
				else             return 0;
			}
			
			template<typename t_data>
			TopN compute_posterior(t_data& data){
				/**
				 * @brief Returns a NEW TopN where each current hypothesis is evaluated on the data. NOTE: If a hypothesis has a new posterior of -inf or NaN, it won't be added. 
				 * @param data
				 * @return 
				 */
				
				// NOTE: Since this modifies the hypotheses, it returns a NEW TopN
				TopN o(N);
				for(auto h : values() ){
					h.compute_posterior(data);
					o << h;
				}
				return o;
			}
			
		};

		// Handy to define this so we can put TopN into a set
		template<typename HYP>
		void operator<<(std::set<HYP>& s, TopN<HYP>& t){ 
			for(auto h: t.values()) {
				s.insert(h);
			}
		}


	}
}
