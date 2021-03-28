#pragma once 

/**
 * @class SpinLock
 * @author piantado
 * @date 28/06/20
 * @file FullMCTS.h
 * @brief A smaller spinlock than std::mutex (used in MCTS where space is a premium)
 */
class SpinLock {
	//https://stackoverflow.com/questions/26583433/c11-implementation-of-spinlock-using-atomic
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) { ; }
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
};

