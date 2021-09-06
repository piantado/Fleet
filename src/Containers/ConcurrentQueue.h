#pragma once 

#include <vector>
#include <mutex>
#include <condition_variable>

#include "OrderedLock.h"

/**
 * @class ConcurrentQueue
 * @author Steven Piantadosi
 * @date 31/08/21
 * @file ConcurrentQueue.h
 * @brief A concurrent queue class that allows multiple threads to push and consume. Note that this
 *        has a fixed, finite size (n) and when its full, we'll block. This prevents us from 
 *        eating up too much memory. 
 */

template<typename T>
class ConcurrentQueue {
	
	size_t N;  // length of the queue

	std::vector<T> to_yield;

	std::atomic<size_t> push_idx;
	std::atomic<size_t> pop_idx; 	

	std::condition_variable_any full_cv; // any needed here to use OrderedLock 
	std::condition_variable_any empty_cv;
	
//	mutable std::mutex lock;
	mutable OrderedLock lock; 
	
public:
	
	ConcurrentQueue(size_t n) : N(n), to_yield(n), push_idx(0), pop_idx(0) {
		
	}
	
	void resize(size_t n) {
		assert(n >= 2);
		N = n;
		to_yield.resize(n);
	}
	
	bool empty() { return push_idx == pop_idx; }
	bool full()  { return (push_idx+1) % N == pop_idx; }
	
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
