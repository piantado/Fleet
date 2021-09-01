#pragma once 

#include <vector>
#include <mutex>

template<typename T>
class ConcurrentQueue {
	
	size_t N;  // length of the queue

	std::vector<T> to_yield;

	mutable size_t push_idx;
	mutable size_t pop_idx; 	

	std::condition_variable full_cv; 
	std::condition_variable empty_cv;
	
	mutable std::mutex lock;

public:
	
	ConcurrentQueue(size_t n) : N(n), to_yield(n), push_idx(0), pop_idx(0) {
	}
	
	void resize(size_t n) {
		assert(n >= 2);
		N = n;
		to_yield.resize(n);
	}
	
	bool empty() const { return push_idx == pop_idx; }
	bool full()  const { return (push_idx+1) % N == pop_idx; }
	
	void push(T& x) {		
		std::unique_lock lck(lock);
		
		// if we are here, we must wait until a spot frees up
		while(full()) full_cv.wait(lck);
		
		to_yield[push_idx] = x;

		push_idx = (push_idx + 1) % N;
		
		empty_cv.notify_one();
	}
	
	T pop() {
		std::unique_lock lck(lock);
		
		// we are empty so we must wait
		while(empty()) empty_cv.wait(lck);
		
		T ret = std::move(to_yield[pop_idx]);
		pop_idx = (pop_idx + 1) % N;;

		full_cv.notify_one();		

		return ret;
	}
	
};
