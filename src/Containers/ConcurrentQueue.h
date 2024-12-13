#pragma once 

#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

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
	static const size_t DEFAULT_CONCURRENT_QUEUE_SIZE = 64;
	
	size_t N;  // length of the queue

	// NOTE: We have tried to align to_yield better for multithreading, but nothing seems to improve speed
	std::vector<T> to_yield;

	std::atomic<size_t> push_idx;
	std::atomic<size_t> pop_idx; 	

	std::condition_variable_any full_cv; // any needed here to use OrderedLock 
	std::condition_variable_any empty_cv;
	
	mutable OrderedLock lock; 
	
public:
	
	ConcurrentQueue(size_t n=DEFAULT_CONCURRENT_QUEUE_SIZE) : N(n), to_yield(n), push_idx(0), pop_idx(0) {
		
	}
	
	void resize(size_t n) {
		assert(n >= 2);
		N = n;
		to_yield.resize(n);
	}
	
	bool empty()  { return push_idx == pop_idx; }
	bool full()   { return (push_idx+1) % N == pop_idx; }
	size_t size() { return (pop_idx - push_idx + N) % N; } 
	
	void push(const T& x) {	
	
		std::unique_lock lck(lock);
		
		// if we are here, we must wait until a spot frees up
		while(full()) full_cv.wait(lck);
		
		to_yield[push_idx] = x;

		push_idx = (push_idx + 1) % N;
		assert(push_idx != pop_idx); // better not
		
		empty_cv.notify_one();
	}
	
	T pop() {
		std::unique_lock lck(lock);
		
		// we are empty so we must wait
		while(empty()) empty_cv.wait(lck);
		
		assert(push_idx != pop_idx); // better not
		T ret = std::move(to_yield[pop_idx]);
		pop_idx = (pop_idx + 1) % N;;

		full_cv.notify_one();		

		return ret;
	}
	
};


/**
 * @class ConcurrentQueueRing
 * @author Steven Piantadosi
 * @date 31/08/21
 * @file ConcurrentQueue.h
 * @brief It turns out that ConcurrentQueue is very slow with many threads. A ConcurrentQueueRing
 * 		  stores a separate ConcurrentQueue for each thread, and then loops through them. Each
 * 		  thread is therefore pushing onto a different ConcurrentQueue (NOTE: These must be 
 * 		  ConcurrentQueues instead of e.g. std::queues, since the pusher and the popper are 
 * 		  different threads, so they ened all of their locking). ConcurrentQueueRing 
 *        prevents most of the locking/synchronization slowness, with only a 
 * 		  little bit of wasted time for the yielding thread.
 * 
 * 		  This provides no guarantees on the order of pops, which may vary erratically based on
 * 	 	  occupancy of the underlying queues.
 * 
 * 		  NOTE: A ConcurrentQueueRing is meant to be read by a single thread
 */
template<typename T>
class ConcurrentQueueRing {
	
	size_t nthreads;
	std::vector<ConcurrentQueue<T>> QS;
	std::atomic<size_t> pop_index;
public:
	ConcurrentQueueRing(size_t t) : nthreads(t), QS(nthreads), pop_index(0){
		
	}
	
	void push(const T& item, size_t thr) {
		assert(thr < nthreads); // must have thr >= 0
		QS[thr].push(item);
	}
	
	std::optional<T> pop() {
		
		while(not CTRL_C) {
			// on empty, we just move to the next slot -- no waiting here.
			if(QS[pop_index].empty()) {
				pop_index = (pop_index + 1) % nthreads;
			}
			else {
				return QS[pop_index].pop();
			}
		}
		
		return std::nullopt;
//		assert(false && "*** Should not get here"); return T{};
	}
	
	bool empty() {
		// this, helpfully, leaves pop_index on the next 
		// non-empty queue
		
		auto start_index = size_t(pop_index); // check if we loop around
		
//		while(not CTRL_C) {
		while(true) {
			if(not QS[pop_index].empty()) {
				return false; 
			}
			else {
				pop_index = (pop_index + 1) % nthreads;
				
				if(pop_index == start_index) // if we make it all the way around, we're empty
					return true; 
			}
		}

		// This is what we return on CTRL_C -- I guess we'll call it empty?
//		return false; 
		assert(false && "*** Should not get here"); return false;
	}
	
};
