#pragma once 

#include <atomic>
#include <cassert>
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
 *        has a fixed, finite size and when it is full, push() blocks. This prevents us from 
 *        eating up too much memory. 
 *
 *        It is a ring buffer of N slots holding at most N-1 elements: one slot is always left
 *        unused so that push_idx == pop_idx unambiguously means "empty" and 
 *        (push_idx+1) % N == pop_idx means "full". Consequently N must be at least 2.
 *
 *        Elements live in [pop_idx, push_idx) taken modulo N, and are popped in FIFO order.
 *        push() and pop() block (on full and empty, respectively) and are safe to call from any
 *        number of threads. Waiting threads re-acquire the internal OrderedLock in FIFO order.
 *
 *        empty() is lock-free and returns a snapshot. A "not empty" answer stays true for a
 *        caller that is the *only* consumer (nobody else can remove the element), which is how
 *        ConcurrentQueueRing uses it. With several consumers, treat it as a hint only; pop() is
 *        still correct and will simply block if another consumer took the element first.
 */
template<typename T>
class ConcurrentQueue {
	static const size_t DEFAULT_CONCURRENT_QUEUE_SIZE = 64;
	
	size_t N;  // number of slots in to_yield (capacity is N-1); only modified under lock
	
	// NOTE: We have tried to align to_yield better for multithreading, but nothing seems to improve speed
	std::vector<T> to_yield;

	// Atomic so that empty() can be read without the lock; only modified under lock
	std::atomic<size_t> push_idx;
	std::atomic<size_t> pop_idx; 	

	std::condition_variable_any full_cv; // any needed here to use OrderedLock 
	std::condition_variable_any empty_cv;
	
	mutable OrderedLock lock; 
	
	// These require the caller to hold lock
	bool full_locked()   const { return (push_idx+1) % N == pop_idx; }
	size_t size_locked() const { return (push_idx + N - pop_idx) % N; }
	
public:
	
	/**
	 * @brief Construct a queue with n slots, holding at most n-1 elements.
	 * @param n -- number of slots, must be >= 2
	 */
	ConcurrentQueue(size_t n=DEFAULT_CONCURRENT_QUEUE_SIZE) : N(n), to_yield(n), push_idx(0), pop_idx(0) {
		assert(n >= 2 && "*** ConcurrentQueue needs at least 2 slots (it holds at most n-1 elements)");
	}
	
	/**
	 * @brief Change the number of slots to n, keeping all current elements in order. The current
	 *        elements must fit (size() <= n-1). Threads blocked in push() are woken, since 
	 *        there may now be room for them.
	 * @param n -- new number of slots, must be >= 2
	 */
	void resize(size_t n) {
		assert(n >= 2 && "*** ConcurrentQueue needs at least 2 slots (it holds at most n-1 elements)");
		
		std::unique_lock lck(lock);
		
		const size_t count = size_locked();
		assert(count <= n-1 && "*** Cannot resize ConcurrentQueue below its current number of elements");
		
		// lay the current elements out contiguously starting at slot 0
		std::vector<T> next(n);
		for(size_t i=0;i<count;i++) {
			next[i] = std::move(to_yield[(pop_idx + i) % N]);
		}
		
		to_yield = std::move(next);
		N = n;
		pop_idx  = 0;
		push_idx = count;
		
		full_cv.notify_all();
	}
	
	/**
	 * @brief Is the queue empty? Lock-free snapshot; see the class documentation.
	 */
	bool empty() const { return push_idx == pop_idx; }
	
	/**
	 * @brief Is the queue full (holding N-1 elements)? A snapshot, which may be stale by the time it returns.
	 */
	bool full() const { 
		std::unique_lock lck(lock);
		return full_locked();
	}
	
	/**
	 * @brief Number of elements currently stored. A snapshot, which may be stale by the time it returns.
	 */
	size_t size() const { 
		std::unique_lock lck(lock);
		return size_locked();
	} 
	
	/**
	 * @brief Add x to the back of the queue, blocking while the queue is full.
	 * @param x
	 */
	void push(const T& x) {	
	
		std::unique_lock lck(lock);
		
		// if we are here, we must wait until a spot frees up
		while(full_locked()) full_cv.wait(lck);
		
		to_yield[push_idx] = x;

		push_idx = (push_idx + 1) % N;
		assert(push_idx != pop_idx); // better not
		
		empty_cv.notify_one();
	}
	
	/**
	 * @brief Remove and return the front of the queue, blocking while the queue is empty.
	 * @return 
	 */
	T pop() {
		std::unique_lock lck(lock);
		
		// we are empty so we must wait
		while(push_idx == pop_idx) empty_cv.wait(lck);
		
		T ret = std::move(to_yield[pop_idx]);
		pop_idx = (pop_idx + 1) % N;

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
 * 		  different threads, so they need all of their locking). ConcurrentQueueRing 
 *        prevents most of the locking/synchronization slowness, with only a 
 * 		  little bit of wasted time for the yielding thread.
 * 
 * 		  Pops are served round-robin: after each successful pop, the ring moves on to the next
 * 		  thread's queue. This guarantees that no producer starves. (If the ring stayed on one 
 * 		  queue until it emptied, a producer that refills its queue faster than the consumer
 * 		  drains it would monopolize the consumer, and every other producer would block forever
 * 		  on its full queue.) Within one thread's queue, items come out in the order pushed; 
 * 		  across threads there is no ordering guarantee.
 * 
 * 		  Threading contract:
 * 		    - push(item, thr) may be called concurrently from many threads; thr must be < nthreads.
 * 		      Each queue is a ConcurrentQueue, so pushes block while that thread's queue is full.
 * 		    - pop() and empty() must be called from a single consumer thread only. They move
 * 		      pop_index without synchronization between them, and pop() relies on the fact that 
 * 		      nobody else can drain a queue it has just seen as non-empty.
 */
template<typename T>
class ConcurrentQueueRing {
	
	size_t nthreads;
	std::vector<ConcurrentQueue<T>> QS;
	std::atomic<size_t> pop_index; // next queue to serve; only modified by the consumer thread
public:
	/**
	 * @brief Construct a ring with one queue per producer thread.
	 * @param t -- number of producer threads, must be >= 1
	 */
	ConcurrentQueueRing(size_t t) : nthreads(t), QS(nthreads), pop_index(0){
		assert(nthreads >= 1 && "*** ConcurrentQueueRing needs at least one thread");
	}
	
	/**
	 * @brief Push item onto thread thr's queue, blocking while that queue is full.
	 * @param item
	 * @param thr -- index of the calling producer thread, in [0, nthreads)
	 */
	void push(const T& item, size_t thr) {
		assert(thr < nthreads && "*** ConcurrentQueueRing::push thread index out of range");
		QS[thr].push(item);
	}
	
	/**
	 * @brief Try to pop from the current queue. Never blocks. On success, returns the item and
	 * 		  moves to the next queue (round-robin). If the current queue is empty, moves to the
	 * 		  next queue and returns std::nullopt, so repeated calls cycle through all queues.
	 * 		  Calling empty() first positions the ring on a non-empty queue, so a pop() right 
	 * 		  after empty() returned false always succeeds. Consumer thread only.
	 * @return 
	 */
	std::optional<T> pop() {
		
		const size_t i = pop_index;
		pop_index = (i + 1) % nthreads;
		
		// on empty, we just move to the next slot -- no waiting here.
		if(not QS[i].empty()) {
			return QS[i].pop();
		}
		
		return std::nullopt;
	}
	
	/**
	 * @brief Are all queues empty? If not, this leaves pop_index on the next non-empty queue
	 * 		  (starting the search at the current pop_index), so that pop() serves it. 
	 * 		  A snapshot: producers may push right after this returns true. Consumer thread only.
	 * @return 
	 */
	bool empty() {
		
		for(size_t k=0;k<nthreads;k++) {
			if(not QS[pop_index].empty()) {
				return false; 
			}
			pop_index = (pop_index + 1) % nthreads;
		}
		
		// we made it all the way around (back to where we started), so all are empty
		return true;
	}
	
};
