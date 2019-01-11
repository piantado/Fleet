#pragma once 

#include <mutex>

template<typename T>
class FiniteHistory {
	// store a finite set of previous samples of T
	// allowing us to compute prior means, etc
public:
	std::vector<T> history;
	std::atomic<size_t> history_size;
	std::atomic<size_t> history_index;
	std::atomic<unsigned long> N;
	std::mutex mutex;
	
	FiniteHistory(size_t n) : history_size(n), history_index(0), N(0) {
		
	}
	
	// default size
	FiniteHistory() : history_size(100), history_index(0), N(0) { 
	}
	
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
	
	void operator<<(T x) { add(x); }
	
	double mean() {
		std::lock_guard guard(mutex);
		double sm=0;
		for(auto a: history) sm += a;
		return double(sm) / history.size();
	}
	
};
