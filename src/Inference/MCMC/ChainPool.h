#pragma once

#include <thread>

//#define DEBUG_CHAINPOOL

#include <vector>

#include "Errors.h"
#include "MCMCChain.h"
#include "Timing.h"
#include "ThreadedInferenceInterface.h"
#include "OrderedLock.h"

/**
 * @class ChainPool
 * @author steven piantadosi
 * @date 29/01/20
 * @file ChainPool.h
 * @brief A ChainPool stores a bunch of MCMCChains and allows you to run them serially or in parallel. 
 * 		  NOTE: When you use a ChainPool, the results will not be reproducible with seed because timing determines when you
 *        switch chains. 
 */
template<typename HYP, typename Chain_t=MCMCChain<HYP>>
class ChainPool : public ThreadedInferenceInterface<HYP> { 
public:

	// the pool stores a bunch of chains
	std::vector<Chain_t> pool;
	
	// these parameters define the amount of a thread spends on each chain before changing to another
	// NOTE: these interact with ParallelTempering swap/adapt values (because if these are too small, then
	// we won't have time to update every chain before proposing more swaps)
	// NOTE: It seems probably better to set steps rather than time, because the hot chains run *much* faster
	// than the cold chains, typically. This means that if you set it by time, then you are spending lots of
	// time on the bad chains, which is the opposite of what you want. Actually, here, we probably should 
	// run for *less* samples on the hot chains because they are faster to sample from.
	// Also note that due to multithreading, this is actually a little complex, we can't perfectly get the 
	// number of samples
	unsigned long steps_before_change = 100;
	
	// Store which chains are running and which are done
	enum class RunningState {READY, RUNNING, DONE};

	// keep track of which threads are currently running
	std::vector<RunningState> running;
	OrderedLock running_lock;
	
	ChainPool() {}
	
	ChainPool(HYP& h0, typename HYP::data_t d, size_t n) {
		assert(n>=1 && "*** You probably shouldn't have a chain pool with 0 elements");
		for(size_t i=0;i<n;i++) {
			add_chain(i==0?h0:h0.restart(), d);
		}
	}
		
	/**
	 * @brief Set this data
	 * @param d -- what data to set
	 * @param recompute -- should I recompute all of the posteriors?
	 */	
	void set_data(typename HYP::data_t d, bool recompute=true) {
		for(auto& c : pool) {
			c.set_data(d, recompute);
		}
	}
	
	/**
	 * @brief Lock and modify the pool
	 * @param h0
	 * @param d
	 */	
	template<typename... ARGS>
	void add_chain(ARGS... args) {
		
		std::lock_guard guard(running_lock);
		
		pool.emplace_back(args...);
		running.push_back(RunningState::READY);
	}
	
	
	size_t nchains() const {
		return pool.size();
	}
	
	void show(std::string prefix) const { 
		for(size_t i=0;i<nchains();i++) {
			std::lock_guard guard(this->pool[i].current_mutex);
			print(prefix, i, (double)this->pool[i].temperature, this->pool[i].getCurrent().posterior, this->pool[i].getCurrent());
		}
	}
	
	/**
	 * @brief This run helper is called internally by multiple different threads, and runs a given pool.
	 * @param ctl
	 */
	generator<HYP&> run_thread(Control& ctl) override {
		assert(pool.size() > 0 && "*** Cannot run on an empty ChainPool");
		assert(this->nthreads() <= pool.size() && "*** Cannot have more threads than chains");
		
		// We have to manage subthreads pretty differently depending on whether we have a time or a 
		// sample constraint. For now, we assume we can't have both
		
		// Here we don't care about being precise about the number of steps 
		// (below we do)		
		
		if(ctl.steps == 0) {
			// we end up here if they're both zero, or steps=0. If they're both 0, we are running till CTRL_C
			
			while( ctl.running() and (not CTRL_C) ) {
				// find the next open thread
				size_t idx;
				{
					std::lock_guard guard(running_lock);
				
					do { 
						idx = this->next_index() % pool.size();
					} while( running[idx] != RunningState::READY ); // so we exit on a false running idx
					running[idx] = RunningState::RUNNING; // say I'm running this one 
				}
				
				// Actually run and yield, being sure to save where everything came from 
				Control c = ctl; // make a copy of everything in control
				c.steps = steps_before_change; c.nthreads = 1; c.runtime = 0; // but update to 
				for(auto x : pool[idx].run(c)) {
					x.born_chain_idx = idx; // set this
					co_yield x;
				}
				
				// and free this lock space
				{
					std::lock_guard guard(running_lock);
					
					ctl.done_steps += steps_before_change-1; // -1 because calling ctl.running adds one
					
					running[idx] = RunningState::READY;					
				}
							
			}
			
			
		}
		else { // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
			assert(ctl.runtime == 0 && "*** Cannot have both time and steps specified in ChainPool (not implemented yet).");
			
			// note here on the while loops, we don't use ctl.running() because we need all the things
			// we start to actually finish (or else we won't run enough steps)
			while( ctl.done_steps < ctl.steps and (not CTRL_C) ) {
				
				// find the next open thread
				size_t idx;
				unsigned long to_run_steps=0;
				{
					std::lock_guard guard(running_lock);
				
					do {
						idx = this->next_index() % pool.size();
					} while( running[idx] != RunningState::READY ); // so we exit on a ready idx
					running[idx] = RunningState::RUNNING; // say I'm running this one 
					
					/// figure out how many steps to run
					to_run_steps = std::min(ctl.steps-ctl.done_steps, this->steps_before_change);								
								
					// exit here if there is nothing else to do
					if(to_run_steps <= 0) {
						break; 
					}	
					
					// now update ctl's number of steps 
					// NOTE if we do this here, we'll stop too early; if we do it later, we'll run too many...
					// hmm.... Need a more complex solution it seems...
					ctl.done_steps += to_run_steps;
					
//					print(">>", idx, ctl.steps, ctl.done_steps, to_run_steps, this->steps_before_change);
				}
				
			
				// Actually run and yield, being sure to save where everything came from 
				Control c = ctl; // make a copy of everything in control
				c.steps = to_run_steps; c.nthreads = 1; c.runtime = 0; // but update to 
				for(auto& x : pool[idx].run(c)) {
					x.born_chain_idx = idx; // set this
					co_yield x;
				}
				
				#ifdef DEBUG_CHAINPOOL
					COUT "# Thread " <<std::this_thread::get_id() << " stopping chain "<< idx TAB "at " TAB chain.current.posterior TAB chain.current.string() ENDL;
				#endif
				
				// and free this lock space
				{
					std::lock_guard guard(running_lock);
						
					// we are done if we ran out of steps to continue running. 
					if(to_run_steps == steps_before_change) {
						running[idx] = RunningState::READY;
					}
					else {
						running[idx] = RunningState::DONE; // say I'm running this one 
					}
				}
			}
			
			
		}
		
		
	}
	
};