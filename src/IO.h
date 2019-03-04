#pragma once


#include <mutex>

std::mutex output_lock;
/* Define some macros that make handling IO a little easier */

#define PRINT(x) { output_lock.lock(); std::cout << x; output_lock.unlock() }

#define PRINTN(x) { output_lock.lock(); std::cout << x << std::endl; output_lock.unlock() }