#pragma once

#include <thread>
#include <mutex>
#include <atomic>

#include <sys/syscall.h>
#include <sys/random.h>

/**
 * @class Rng
 * @author Steven Piantadosi
 * @date 15/06/21
 * @file Random.h
 * @brief This is a thread_local rng. The first one constructed (on the main thread) is the
 * 		  "base"; Fleet seeds it via --seed=X. Rngs constructed later (one per additional
 * 		  thread) derive their seed deterministically from that global seed plus an arrival
 * 		  counter.
 *
 * 		  NOTE: this used to seed each new thread's rng by DRAWING numbers from the base
 * 		  (main thread's) generator. That was (a) a data race -- the constructor's mutex only
 * 		  excluded other constructors, not the main thread concurrently using its own
 * 		  generator -- and (b) dependent on the order in which threads happened to start.
 * 		  Deriving from {seed, counter} fixes the race and makes each thread's *stream* a
 * 		  pure function of the seed and its arrival index. Be aware that this still does not
 * 		  make multithreaded inference reproducible: which chain runs on which thread (and
 * 		  for how long) is timing-dependent (see the note in ChainPool), so run-to-run
 * 		  reproducibility with --seed only holds for single-threaded runs.
 */
thread_local class Rng : public std::mt19937 {
public:

	// The first of these that is made is called the "base".
	static std::mutex mymut;
	static Rng* base;

	// the global --seed value (0 means unseeded); set via seed() on the base before any
	// worker threads are started
	static std::atomic<unsigned long> base_seed;

	// how many non-base Rngs have been constructed; used to give each thread its own stream
	static std::atomic<unsigned long> thread_counter;

	Rng() : std::mt19937() {

		// allow only one constructor at a time since we need to set the base
		std::lock_guard guard(mymut);

		if(base == nullptr) {
			// on construction we initialize from /dev/urandom
			// (if --seed was given, Fleet::initialize re-seeds this via seed() below)
			std::array<unsigned long, std::mt19937::state_size> state;
			sysrandom(state.begin(), state.size()*sizeof(unsigned long));
			std::seed_seq seedseq(state.begin(), state.end());
			this->std::mt19937::seed(seedseq);

			base = this;
		}
		else {
			randomize();
		}
	}

	void randomize() {
		// Give this rng its own stream. If a global seed was set, derive deterministically
		// from {seed, arrival index}; otherwise seed from /dev/urandom.
		// NOTE: we deliberately do NOT draw from base here (see the class comment).
		const unsigned long k = ++thread_counter;
		const unsigned long s = base_seed.load();

		if(s != 0) {
			std::seed_seq seedseq{ (unsigned int)(s & 0xFFFFFFFFul),
								   (unsigned int)(s >> 32),
								   (unsigned int)(k & 0xFFFFFFFFul),
								   (unsigned int)(k >> 32) };
			this->std::mt19937::seed(seedseq);
		}
		else {
			std::array<unsigned long, std::mt19937::state_size> state;
			sysrandom(state.begin(), state.size()*sizeof(unsigned long));
			std::seed_seq seedseq(state.begin(), state.end());
			this->std::mt19937::seed(seedseq);
		}
	}

	/**
	 * @brief Seed only if s is nonzero. (Called by Fleet::initialize on the main thread's
	 * 		  rng; also records the seed so later threads can derive theirs from it.)
	 */
	 void seed(unsigned long s) {
		 base_seed = s;
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
std::atomic<unsigned long> Rng::base_seed = 0;
std::atomic<unsigned long> Rng::thread_counter = 0;
