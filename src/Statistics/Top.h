
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
//		add(x);
		cnt = x.cnt;
		s =  x.s;
		print_best = x.print_best;
	}
	

	void set_size(size_t n) {
		// DOES NOT RESIZE
		N = n;
	}
	void set_print_best(bool b) {
		print_best = b;
	}

	size_t size() const {
		// returns the NUMBER in the set, not the total number allowed!
		return s.size();
	}
	bool empty() const {
		return s.size() == 0;
	}
	
	std::multiset<T>& values(){
		return s;
	}

	void add(const T& x, size_t count=1) { 
		// add something of type x if we should - only makes a copy if we add it
		// NOTE: we do not add -inf things
		
		if(std::isnan(x.posterior) or x.posterior == -infinity or N == 0) return;
		
		std::lock_guard guard(lock);

		// if we aren't in there and our posterior is better than the worst
		if(s.find(x) == s.end() and 
			(s.size() < N or x.posterior > s.begin()->posterior)) { // skip adding if its the worst
			T xcpy = x;
		
			if(print_best && (empty() or x.posterior > best().posterior))  {
				xcpy.print();
			}
		
			s.insert(xcpy); // add this one
//			assert(cnt.find(xcpy) == cnt.end());
			cnt[xcpy] = count;
			
			// and remove until we are the right size
			while(s.size() > N) {
				size_t n = cnt.erase(*s.begin());
				assert(n==1);
				s.erase(s.begin()); 
			}
		}
		else { // if its stored somewhere already
			cnt[x] += count;
		}
	}
	void operator<<(const T& x) {
		add(x);
	}
	
	void add(const TopN<T>& x) { // add from a whole other topN
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
	
	
    const T& best() { 
		assert( (!s.empty()) && "You tried to get the max from a TopN that was empty");
		return *s.rbegin();  
	}
    const T& worst() { 
		assert( (!s.empty()) && "You tried to get the min from a TopN that was empty");
		return *s.begin(); 
	}
	
	double best_score() {
		std::lock_guard guard(lock);
		if(s.empty()) return -infinity;
		return s.rbegin()->posterior;  
	}
	double worst_score() {
		std::lock_guard guard(lock);
		if(s.empty()) return infinity;
		return s.begin()->posterior;  
	}
	
    bool empty() { return s.empty(); }
    
	double Z() { // compute the normalizer
		double z = -infinity;
		std::lock_guard guard(lock);
		for(const auto& x : s) 
			z = logplusexp(z, x.posterior);
		return z;       
	}
	
	void print(std::string prefix="") {
		
		// this sorts so that the highest probability is last
		std::vector<T> v(s.size());
		std::copy(s.begin(), s.end(), v.begin());
		std::sort(v.begin(), v.end()); 
		
		for(auto& h : v) {
			h.print(prefix);
		}
	}
	
	void clear() {
		std::lock_guard guard(lock);
		s.erase(s.begin(), s.end());
		cnt.clear();
	}
	
	unsigned long count(const T x) {
		std::lock_guard guard(lock);
		// This mightt get called by something not in here, so we can't assume x is in 
		if(cnt.count(x)) return cnt.at(x);
		else             return 0;
	}
 	
	template<typename t_data>
	TopN compute_posterior(t_data& data){
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
