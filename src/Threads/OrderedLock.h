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
public:
    std::queue<std::condition_variable*> cvar;
    std::mutex                           cvar_lock;
    bool                                 locked;

    OrderedLock() : locked(false) {}
	
    void lock() {
        std::unique_lock<std::mutex> acquire(cvar_lock);
        if(locked) {
            std::condition_variable signal;
            cvar.emplace(&signal);
            signal.wait(acquire);
        } 
		else {
            locked = true;
        }
    }
	
    void unlock() {
        std::unique_lock<std::mutex> acquire(cvar_lock);
        if(cvar.empty()) {
            locked = false;
        } 
		else {
            cvar.front()->notify_one();
            cvar.pop();
        }
    }
};