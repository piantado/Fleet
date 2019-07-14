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
	
	mutable std::mutex lock;
	
protected:
	std::map<T,unsigned long> cnt; // also let's count how many times we've seen each for easy debugging
	std::multiset<T> s; // important that it stores in sorted order by posterior! Multiset because we may have multiple samples that are "equal" (as in SymbolicRegression)
	
public:
	size_t N;
	
	TopN(size_t n=std::numeric_limits<size_t>::max()) : N(n) {}
	
	TopN(const TopN<T>& x) {
		clear();
		set_size(x.size());
		add(x);
	}
	TopN(TopN<T>&& x) {
		cnt = std::move(x.cnt);
		set_size(x.size());
		s = std::move(x.s);
	}
	
	void operator=(const TopN<T>& x) {
		clear();
		set_size(x.N);
		add(x);
	}
	void operator=(TopN<T>&& x) {
		set_size(x.N);
		cnt = std::move(x.cnt);
		s = std::move(x.s);
	}
	

	void set_size(size_t n) {
		N = n;
	}

	size_t size() const {
		return s.size();
	}
	
	std::multiset<T>& values(){
		return s;
	}

	void add(const T& x) { 
		// add something of type x if we should - only makes a copy if we add it
		
		// toss out infs
		if(std::isnan(x.posterior) || x.posterior == -infinity) return;
		
		std::lock_guard guard(lock);
		
		// if we aren't in there and our posterior is better than the worst
		if(s.find(x) == s.end()) { 
			if(s.size() < N || s.empty() || x.posterior > s.begin()->posterior) { // skip adding if its the worst
				T xcpy = x;
			
				s.insert(xcpy); // add this one
				assert(cnt.find(xcpy) == cnt.end());
				cnt[xcpy] = 1;
				
				// and remove until we are the right size
				while(s.size() > N) {
					size_t n = cnt.erase(*s.begin());
					assert(n==1);
					s.erase(s.begin()); 
				}
				
			}
		}
		else { // if its stored somewhere already
			cnt[x]++;
		}
	}
	void operator<<(const T& x) {
		add(x);
	}
	
	void add(const TopN<T>& x) { // add from a whole other topN
		for(auto& h: x.s){
			add(h);
		}
	}
	void operator<<(const TopN<T>& x) {
		add(x);
	}
	
    T best() { 
		assert( (!s.empty()) && "You tried to get the max from a TopN that was empty");
		return *s.rbegin();  
	}
    T worst() { 
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
		for(auto x : s) z = logplusexp(z, x.posterior);
		return z;       
	}
	
    void print(void printer(T&)) {
		std::lock_guard guard(lock);
		for(auto h : s) {
			printer(h);
		}
    }
	
	void print(std::string prefix="") {
		for(auto& h : s) {
			COUT prefix << cnt[h] TAB h.posterior TAB h.prior TAB h.likelihood TAB q(h.string()) ENDL;
		}
	}
	
	void clear() {
		std::lock_guard guard(lock);
		s.erase(s.begin(), s.end());
		cnt.clear();
	}
	
	unsigned long count(const T x) const {
		// This migth get called by something not in here, so we can't assume x is in 
		if(cnt.count(x)) return cnt.at(x);
		else             return 0;
	}
	
};

// Handy to define this so we can put TopN into a set
template<typename HYP>
void operator<<(std::set<HYP>& s, TopN<HYP>& t){ 
	for(auto h: t.values()) {
		s.insert(h);
	}
}
