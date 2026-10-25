#pragma once

#include <cassert>
#include <mutex>
#include <condition_variable>
#include <queue>

/**
 * @class OrderedLock
 * @author Steven Piantadosi
 * @date 28/03/21
 * @file OrderedLock.h
 * @brief A FIFO mutex: threads acquire the lock in exactly the order in which they called lock().
 *        Adapted from https://stackoverflow.com/questions/14792016/creating-a-lock-that-preserves-the-order-of-locking-attempts-in-c11
 *
 *        Semantics:
 *          - Satisfies BasicLockable (lock/unlock), so it works with std::lock_guard, std::unique_lock,
 *            and std::condition_variable_any. It deliberately does not provide try_lock, so it cannot be
 *            used with std::lock or std::scoped_lock over several mutexes.
 *          - Not recursive: a thread that calls lock() while already holding the lock deadlocks.
 *          - unlock() must only be called while the lock is held (by the thread that holds it, as with
 *            std::mutex). This is checked with an assert.
 *          - Ownership is handed off directly: unlock() with waiters present never clears `locked`; it
 *            passes the lock to the oldest waiter. No newly arriving thread can barge in between, because
 *            lock() only takes the fast path when the lock is free *and* nobody is queued.
 *
 *        Implementation notes:
 *          - Each waiter owns a condition variable and a `notified` flag, both on its own stack. Waiting
 *            on the flag (not a bare wait) makes spurious wakeups harmless.
 *          - unlock() sets the flag and notifies while holding the internal mutex. This is required for
 *            lifetime safety: the waiter cannot observe notified==true and return (destroying its Waiter)
 *            until unlock() releases the internal mutex, so the Waiter is alive for the notify_one call.
 */
class OrderedLock {
private:
    struct Waiter {
        std::condition_variable cv;
        bool notified = false;
    };

    std::queue<Waiter*> waiters; // threads blocked in lock(), oldest at the front
    std::mutex mtx;              // protects waiters and locked
    bool locked = false;         // true while some thread owns the OrderedLock

public:
    OrderedLock() = default;
    OrderedLock(const OrderedLock&) = delete;
    OrderedLock& operator=(const OrderedLock&) = delete;

    ~OrderedLock() {
        assert(waiters.empty() && "*** OrderedLock destroyed while threads are waiting on it");
    }

    /**
     * @brief Acquire the lock, blocking until every thread that called lock() earlier has
     *        acquired and released it.
     */
    void lock() {
        std::unique_lock<std::mutex> lk(mtx);

        if (!locked && waiters.empty()) {
            locked = true;
            return;
        }

        Waiter waiter;
        waiters.push(&waiter);

        waiter.cv.wait(lk, [&] {
            return waiter.notified;
        });

        // ownership was handed to us by unlock(), which left locked == true
        assert(locked);
    }

    /**
     * @brief Release the lock, handing it directly to the oldest waiter if there is one.
     */
    void unlock() {
        std::unique_lock<std::mutex> lk(mtx);

        assert(locked && "*** OrderedLock::unlock() called on a lock that is not held");

        if (waiters.empty()) {
            locked = false;
            return;
        }

        Waiter* next = waiters.front();
        waiters.pop();

        next->notified = true;
        next->cv.notify_one();
    }
};
