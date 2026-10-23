#pragma once

#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>

/**
 * @class OrderedLock
 * @author Steven Piantadosi
 * @date 28/03/21
 * @file OrderedLock.h
 * @brief A FIFO mutex (from stackoverflow)  https://stackoverflow.com/questions/14792016/creating-a-lock-that-preserves-the-order-of-locking-attempts-in-c11
 */

class OrderedLock {
private:
    struct Waiter {
        std::condition_variable cv;
        bool notified = false;
    };

    std::queue<Waiter*> waiters;
    std::mutex mtx;
    bool locked = false;

public:
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
    }

    void unlock() {
        std::unique_lock<std::mutex> lk(mtx);

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