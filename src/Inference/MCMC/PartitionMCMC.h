#pragma once

#include <thread>
#include <atomic>
#include <limits>

//#define DEBUG_PARTITION_MCMC 1

#include <vector>

#include "Errors.h"
#include "MCMCChain.h"
#include "ChainPool.h"
#include "Timing.h"
#include "ThreadedInferenceInterface.h"

/**
 * @brief The result of get_partitions:
 *        partial  -- trees with holes (gaps), each of which defines one partition (the set of its completions)
 *        complete -- complete trees that were reached during expansion. These are single hypotheses that are
 *                    NOT inside any partial partition, so they must be evaluated on their own.
 *        Together these are an exact (disjoint and exhaustive) cover of all trees below h0: every complete tree
 *        below h0 is either in exactly one element of `complete` or a completion of exactly one element of `partial`.
 */
template<typename HYP>
struct Partitions {
	std::vector<HYP> partial;
	std::vector<HYP> complete;
};

/**
 * @brief Compute "partitions" meaning trees with holes where we can run MCMC on the holes. We expand the first
 *        (leftmost) gap of every partial tree, one level at a time, for up to max_depth levels. Expansions that
 *        become complete trees are moved into `complete` (and not expanded further); the partial trees at the
 *        final level are the partitions. If max_size>0, we stop before a level would make more than max_size
 *        partial trees (keeping the previous level). At least one of max_depth, max_size must be nonzero.
 *
 *        Why this is a disjoint cover: because we always expand the first gap, every complete tree T below h0 has
 *        a unique sequence of prefixes, one per level. At each level, T's prefix is either T itself (then T was put
 *        in `complete` at that level and its prefix is never expanded again) or a partial tree in that level.
 *        Each level is built fresh from the previous one only (it does not accumulate earlier levels), so the
 *        final partial trees cannot contain one another.
 *
 *        NOTE: each partial tree is generated exactly once, so no deduplication (std::set) is needed.
 * @param h0
 * @param max_depth
 * @param max_size
 * @return
 */
template<typename HYP>
Partitions<HYP> get_partitions(const HYP& h0, const size_t max_depth, const size_t max_size) {
	assert(not (max_depth==0 and max_size==0));
	assert(not h0.is_evaluable() && "*** get_partitions requires a partial h0 (usually empty)");

	Partitions<HYP> out;
	out.partial = {h0};

	for(size_t dd=0;(max_depth==0 or dd<max_depth) and (max_size==0 or out.partial.size() < max_size);dd++) {

		if(CTRL_C) break;

		std::vector<HYP> nxt;  // the next level -- built fresh from this level only
		std::vector<HYP> done; // complete trees reached at this level

		for(auto& h: out.partial) {
			const int neigh = h.neighbors();
			for(int n=0;n<neigh;n++) {
				auto newh = h.make_neighbor(n); // NOTE: this compiles newh if it is complete
				if(newh.is_evaluable()) done.push_back(std::move(newh));
				else                    nxt.push_back(std::move(newh));
			}
		}

		// if this level would make too many partitions, keep the previous one. We must also drop
		// this level's complete trees, since they are completions of the partitions we keep
		if(max_size > 0 and nxt.size() > max_size) {
			break;
		}

		out.complete.insert(out.complete.end(), std::make_move_iterator(done.begin()), std::make_move_iterator(done.end()));
		out.partial = std::move(nxt);

		if(out.partial.empty()) break; // everything is complete (finite space)
	}

	return out;
}


/**
 * @class PartitionMCMC
 * @author Steven Piantadosi
 * @date 26/12/20
 * @file PartitionMCMC.h
 * @brief This takes a grammar and expands to a given depth, and then runs a separate MCMC chain in each
 * 		  partial subtree that is generated. The complete trees found during expansion (small hypotheses that
 *        no partition contains) are each evaluated and yielded exactly once by run(), before any chain samples,
 *        so that together they cover the whole hypothesis space. (These have born_chain_idx = COMPLETE_TREE_IDX.)
 *        NOTE: the yielded samples, pooled over chains, are not samples from the posterior -- each chain spends
 *        the same effort regardless of its partition's mass. This is intended for search (e.g. TopN).
 */
template<typename HYP>
class PartitionMCMC : public ChainPool<HYP> {
public:

	static constexpr size_t COMPLETE_TREE_IDX = std::numeric_limits<size_t>::max();

	// the complete trees (not in any partition); their posteriors are computed in the constructor and in set_data
	std::vector<HYP> complete_trees;

	// has some thread claimed yielding the complete trees? (only one thread yields them, once per object)
	std::atomic<bool> complete_claimed{false};

	/**
	 * @brief This specifies both a max_Size and a max_depth which are the most partitions we'll make. If max_size=0
	 *        then we ignore it
	 * @param h0
	 * @param max_depth
	 * @param data
	 * @param max_size
	 */
	PartitionMCMC(HYP& h0, size_t max_depth, typename HYP::data_t data, size_t max_size=0) {

		assert(not h0.is_evaluable() && "*** You should not call PartitionMCMC on a complete node (usually you want it to be empty)");

		auto parts = get_partitions(h0, max_depth, max_size);

		for(auto& h : parts.complete) {
			h.compute_posterior(data);
			h.born_chain_idx = COMPLETE_TREE_IDX;
		}
		complete_trees = std::move(parts.complete);

		// now make an MCMC chain on each of these
		// (all must use the callback)
		this->pool.reserve(parts.partial.size()); // MCMCChain's move is not noexcept, so growing would copy chains (and data)
		for(auto& h: parts.partial) {
			HYP x = h;
			for(auto& n : x.get_value() ){
				n.can_resample = false;
			}

			x.complete(); // fill in any structural gaps (new nodes are can_resample=true)

			this->pool.push_back(MCMCChain<HYP>(x, data));
			this->running.push_back(ChainPool<HYP>::RunningState::READY);

			#ifdef DEBUG_PARTITION_MCMC
				print("Starting PartitionMCMC on ", h.string(), "\t", x.string());
			#endif
		}

		if(FleetArgs::print_header) { // (off in the unit tests, which must print one line to stdout)
			print("# Initialized ", this->pool.size(), " partitions and ", complete_trees.size(), " complete trees");
		}
	}

	/**
	 * @brief Set the data, recomputing the complete trees' posteriors too
	 *        (this hides ChainPool::set_data, which is not virtual)
	 */
	void set_data(typename HYP::data_t d, bool recompute=true) {
		if(recompute) {
			for(auto& h : complete_trees) h.compute_posterior(d);
		}
		ChainPool<HYP>::set_data(d, recompute);
	}

	/**
	 * @brief One thread first yields every complete tree (once per PartitionMCMC object), then every thread
	 *        runs the chains as in ChainPool. The complete trees are yielded regardless of ctl's steps/time
	 *        (they do not count toward steps), but not after CTRL_C.
	 */
	generator<HYP&> run_thread(Control& ctl) override {

		if(not complete_claimed.exchange(true)) {
			for(auto& h : complete_trees) {
				if(CTRL_C) break;
				co_yield h; // the caller (run_thread_generator_wrapper) copies this into the output queue
			}
		}

		if(this->pool.empty()) co_return; // everything was a complete tree

		for(auto& x : ChainPool<HYP>::run_thread(ctl)) {
			co_yield x;
		}
	}

};
