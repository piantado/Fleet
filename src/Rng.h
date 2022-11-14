#pragma once 

#include <thread>
#include <mutex>

#include <sys/syscall.h>
#include <sys/random.h>

/**
 * @class Rng
 * @author Steven Piantadosi
 * @date 15/06/21
 * @file Random.h
 * @brief This is a thread_local rng whose first object is used to see others (in other threads). This way,
 * 		  we can have thread_local rngs that all are seeded deterministcally in Fleet via --seed=X.
 */
thread_local class Rng : public std::mt19937 {
public:

	// The first of these that is made is called the "base" and is used to seed any others.
	// Because this variable rng is thread_local, each thread will get its own and its seed
	// will be determined (deterministically) from base. 
	static std::mutex mymut; 
	static Rng* base; 

	Rng() : std::mt19937() {
		
		// allow only one constructor at a time
		std::lock_guard guard(mymut);
		
		if(base == nullptr) {
			// on construction we initialize from /dev/urandom
			std::array<unsigned long, std::mt19937::state_size> state;
			sysrandom(state.begin(), state.size()*sizeof(unsigned long));
			std::seed_seq seedseq(state.begin(), state.end());
			this->std::mt19937::seed(seedseq);
			
			base = this;
		}
		else {
			
			// seed from base -- so we get replicable numbers assuming the 
			// order of construction is fixed
			std::array<unsigned long, std::mt19937::state_size> state;
			for(size_t i=0;i<state.size();i++){
				state[i] = base->operator()();
			}
			std::seed_seq seedseq(state.begin(), state.end());
			this->std::mt19937::seed(seedseq);
		}
	}
	
	/**
	 * @brief Seed only if s is nonzero
	 */	
	 void seed(unsigned long s) {
		 if(s != 0) {
			 this->std::mt19937::seed(s);
		 }
	 }
	 
	static size_t sysrandom(void* dst, size_t dstlen) {
		// from https://stackoverflow.com/questions/45069219/how-to-succinctly-portably-and-thoroughly-seed-the-mt19937-prng
		char* buffer = reinterpret_cast<char*>(dst);
		std::ifstream stream("/dev/urandom", std::ios_base::binary | std::ios_base::in);
		stream.read(buffer, dstlen);
		return dstlen;
	}

	
} DefaultRNG;

std::mutex Rng::mymut;
Rng* Rng::base = nullptr; 