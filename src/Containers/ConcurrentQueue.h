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

	// NOTE: a plain std::mutex + std::condition_variable, not an OrderedLock. This queue has
	// a single consumer and (possibly) many fast producers; with a FIFO lock the consumer 
	// must wait its turn behind every queued producer, so when producers outrun the consumer
	// the whole pipeline throughput collapses to the fair-queueing rate. With a plain mutex 
	// the consumer can take the lock as soon as it is free and drain. 
	std::condition_variable full_cv;
	std::condition_variable empty_cv;
	
	mutable std::mutex lock; 
	
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
	size_t size() { return (push_idx - pop_idx + N) % N; } // NOTE: was (pop-push+N)%N, which gave N-size
	
	void push(const T& x) {	
	
		std::unique_lock lck(lock);
		
		// if we are here, we must wait until a spot frees up
		while(full()) full_cv.wait(lck);
		
		const bool was_empty = empty();
		
		to_yield[push_idx] = x;

		push_idx = (push_idx + 1) % N;
		assert(push_idx != pop_idx); // better not
		
		// Only signal on the empty->nonempty transition: a waiter can only be waiting if it
		// saw the queue empty, so notifying on every push is just wasted syscalls.
		// NOTE: transition-only notify_one assumes at most one consumer and one producer per
		// queue (which is how ConcurrentQueueRing uses these: one queue per producer thread,
		// drained by the single main thread). With multiple consumers a second waiter could
		// sleep through a nonempty queue; notify on every push/pop if you ever need that.
		if(was_empty) empty_cv.notify_one();
	}
	
	T pop() {
		std::unique_lock lck(lock);
		
		// we are empty so we must wait
		while(empty()) empty_cv.wait(lck);
		
		const bool was_full = full();
		
		assert(push_idx != pop_idx); // better not
		T ret = std::move(to_yield[pop_idx]);
		pop_idx = (pop_idx + 1) % N;;

		// only signal on the full->not-full transition (see push)
		if(was_full) full_cv.notify_one();		

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
		
		// on empty, we just move to the next slot -- no waiting here.
		if(not QS[pop_index].empty()) {
			return QS[pop_index].pop();
		}
		
		pop_index = (pop_index + 1) % nthreads;
		
//			print("looping", (size_t)pop_index);
	
		return std::nullopt;
//		assert(false && "*** Should not get here"); return T{};
	}
	
	bool empty() {
		// this, helpfully, leaves pop_index on the next 
		// non-empty queue
		
		size_t start_index = pop_index; // check if we loop around
		
		while(true) {
//			print("here", (size_t)start_index, (size_t)pop_index, QS[pop_index].empty());
			
			if(not QS[pop_index].empty()) {
				return false; 
			}
			else {				
				pop_index = (pop_index + 1) % nthreads;
				
			}
			
			if(pop_index == start_index) // if we make it all the way around, we're empty
				return true; 
			
			
		}

		assert(false && "*** Should not get here"); return false;
	}
	
};
