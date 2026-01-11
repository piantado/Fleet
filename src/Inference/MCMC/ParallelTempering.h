#pragma once 

//#define PARALLEL_TEMPERING_SHOW

// show every swap?
//#define PARALLEL_TEMPERING_SHOW_SWAP

#include <signal.h>
#include <functional>

#include "Errors.h"
#include "ChainPool.h"
#include "Timing.h"

extern std::atomic<bool> CTRL_C; //volatile sig_atomic_t CTRL_C; 

/**
 * @class ParallelTempering
 * @author piantado
 * @date 10/06/20
 * @file ParallelTempering.h
 * @brief This is a chain pool that runs multiple chains on a ladder of different temperatures and adjusts temperatures in order to 
 * 			balances swaps up and down the ladder (which makes it efficient). 
 * 			The adaptation scheme follows https://arxiv.org/abs/1501.05823
 * 			NOTE This starts two extra threads, one for adapting and one for swapping, but they mostly wait around.
 */
template<typename HYP>
class ParallelTempering : public ChainPool<HYP> {
	
public:

	// loops on the swapper and adapter threads wait this long before checking their time, to make them interruptible
	// NOTE that this can throw off timing speeds for e.g. parallel tempering for very small amounts of data
	const unsigned long time_resolution_ms = 25; 
	
	using Super = ChainPool<HYP>;
		
	time_ms swap_every = 250; // try a round of swaps this often 
	time_ms adapt_every = 5000; // 
	time_ms show_every = 10000;
	
	OrderedLock overall_mutex; // This mutex coordinates swappers, adapters, and printers, otherwise they can lock each other out
	
	
	
	// Swap history stores how often the i'th chain swaps with the (i-1)'st chain
	std::vector<FiniteHistory<bool>> swap_history;
	
	bool is_temperature; // set for whether we initialize according to a temperature ladder (true) or data
	
	std::atomic<bool> terminate; // used to kill swapper and adapter
	
	ParallelTempering() : terminate(false) {} 
	
	ParallelTempering(HYP& h0, typename HYP::data_t d, std::initializer_list<double> t) : 
		ChainPool<HYP>(h0, d, t.size()),  terminate(false) {
		
		std::vector<double> temperatures{t};
		
		swap_history.reserve(temperatures.size()); // reserve so we don't move
			
		for(size_t i=0;i<temperatures.size();i++) {
			this->pool[i].temperature = temperatures[i]; // set its temperature
			swap_history.emplace_back();
		}
	}
	
	
	ParallelTempering(HYP& h0, typename HYP::data_t d, unsigned long n, double maxT) : 
		ChainPool<HYP>(h0, d, n),terminate(false) {
		assert(n != 0);
		swap_history.reserve(n);
		
		if(n == 1) { // just enforce this as a hard cosntraint. 
			this->pool[0].temperature = 1.0;
			swap_history.emplace_back();
		}
		else {
			for(size_t i=0;i<n;i++) {
				this->pool[i].temperature = exp(i * log(maxT)/(n-1));
				swap_history.emplace_back();
				//print("t=", double(this->pool[i].temperature));
			}
		}
	}
	
	template<typename... ARGS>
	void add_chain(ARGS... args) {
		
		Super::add_chain(args...);	
		this->swap_history.emplace_back();
	}
	
	// So this can be overwritten if we need a subclass
	virtual void swap(size_t i, size_t j) {
		// swap the chains
		std::swap(this->pool[i].getCurrent(), this->pool[j].getCurrent());
		
		// and let's swap their steps since improvement so that the steps-since-improvement
		// stays with a chain 
		std::swap(this->pool[i].steps_since_improvement, this->pool[j].steps_since_improvement);		
	}
	
	virtual double lp_swap(size_t i, size_t j) {
		// compute the log probability of swapping i and j 

		double Tnow = this->pool[i].at_temperature(this->pool[i].temperature) + 
		              this->pool[j].at_temperature(this->pool[j].temperature);
		double Tswp = this->pool[i].at_temperature(this->pool[j].temperature) + 
					  this->pool[j].at_temperature(this->pool[i].temperature);
		return Tswp-Tnow;		
	}
	
	void __shower_thread() {
		
		while(not (terminate or CTRL_C) ) {
			
			// we will sleep for adapt_every but in tiny chunks so that 
			// we can be stopped with CTRL_C
			auto last = now();
			while(time_since(last) < show_every and (not CTRL_C) and (not terminate))
				std::this_thread::sleep_for(std::chrono::milliseconds(time_resolution_ms)); 
			
			if(CTRL_C or terminate) return;
			
			show_statistics();
		}
		
	}
	
	void __swapper_thread() {
			
		// runs a swapper every swap_every ms (double)
		// NOTE: If we have 1 element in the pool, it is caught below
		while(!(terminate or CTRL_C)) {
			
			// make this interruptible
			auto last = now();
			while(time_since(last) < swap_every and (not CTRL_C) and (not terminate))
				std::this_thread::sleep_for(std::chrono::milliseconds(time_resolution_ms)); 
			
			assert(this->swap_history.size() == this->pool.size());
						
			// Here we are going to go through and swap each chain with its neighbor
			std::lock_guard og(overall_mutex); // must get this so we don't lock by another thread stealing one of below
			for(size_t k=this->pool.size()-1;k>=1;k--) { // swap k with k-1; starting at the bottom so stuff can percolate up
				if(CTRL_C or terminate) break; 
				
				// get both of these thread locks
				std::lock_guard guard1(this->pool[k-1].current_mutex);
				std::lock_guard guard2(this->pool[k  ].current_mutex);
				
				double R = lp_swap(k,k-1);
	
				//DEBUG("Swap p: ", k, R, this->pool[k-1].samples, this->pool[k-1].getCurrent().posterior, this->pool[k-1].getCurrent()); 
				
				if(valid(R) and (R >= 0 or flip(exp(R)))) { 
										
					#ifdef PARALLEL_TEMPERING_SHOW_SWAP
					COUT "# Swapping " <<k<< " and " <<(k-1)<<"." TAB this->pool[k].current.likelihood TAB this->pool[k-1].current.likelihood ENDL;
					COUT "# " TAB this->pool[k].current.string() ENDL;
					COUT "# " TAB this->pool[k-1].current.string() ENDL;
					#endif
					
					this->swap(k,k-1);
					
					swap_history.at(k) << true;
				}
				else {
					swap_history.at(k) << false;
				}
								
			}
		}
	}
	
	void __adapter_thread() {
		
		while(not (terminate or CTRL_C) ) {
			
			// we will sleep for adapt_every but in tiny chunks so that 
			// we can be stopped with CTRL_C
			auto last = now();
			while(time_since(last) < adapt_every and (not CTRL_C) and (not terminate))
				std::this_thread::sleep_for(std::chrono::milliseconds(time_resolution_ms)); 
			
			if(CTRL_C or terminate) return;
			
			// This is not interruptible with CTRL_C: std::this_thread::sleep_for(std::chrono::milliseconds(adapt_every));
			
			#ifdef PARALLEL_TEMPERING_SHOW
				show_statistics();
			#endif
				
			adapt(); // TOOD: Check what counts as t
		}
	}
	
	
	generator<HYP&> run(Control ctl) {
		
//		print("Starting swapper thread");
		
		// Start a swapper and adapter thread
		std::thread swapper(&ParallelTempering<HYP>::__swapper_thread, this); // pass in the non-static mebers like this:
		std::thread adapter(&ParallelTempering<HYP>::__adapter_thread, this);

		#ifdef PARALLEL_TEMPERING_SHOW
		std::thread shower(&ParallelTempering<HYP>::__shower_thread, this);
		#endif

		// run normal pool run
		for(auto& h : ChainPool<HYP>::run(ctl)) {
			co_yield h;
		}
		
		// kill everything else once that thread is done
		terminate = true;
		
		swapper.join();
		adapter.join();
		
		#ifdef PARALLEL_TEMPERING_SHOW
		shower.join();
		#endif

		
	}
	
	void show_statistics() {
		
		// TODO: This raelly needs a lock, but for some reason there is a lock problem that eventually hangs
		// when we do it. Not sure why, very hard to debug/understand. Might be that the current_mutex is held
		// by the generator so when we try to get to this inside the generator loop, its a disaster?
		
		std::lock_guard og(overall_mutex);
		
		print("##############################");	
		print("### Pool info: ###############");
		for(size_t i=0;i<this->pool.size();i++) {
			
			// ugh locking doesn't work here..
			std::unique_lock guard(this->pool[i].current_mutex);
			//auto cpy = this->pool.at(i).current; // otherwise, without a mutex, it can change between accessing string and posterior, which is gnarly
			auto& cpy = this->pool.at(i).getCurrent(); 
			
			print(i, 
					double(this->pool.at(i).temperature),
					this->pool.at(i).samples,
					cpy.posterior,
					cpy.prior,
					cpy.likelihood,
					//this->pool.at(i).acceptance_ratio(),
					swap_history.at(i).mean(),
					//int(swap_history.at(i).N),
					//this->pool.at(i).samples,
					cpy.string()
					);
		}
		print("############################## \n");

	}
	
	double k(unsigned long t, double v, double t0) {
		return (1.0/v) * t0/(t+t0); 
	}
	
	void adapt(double v=3, double t0=1000000) {
		std::lock_guard og(overall_mutex);
		// no total lock necessary since we don't modify anything except the (atomic) temperature
		
		std::vector<double> sw(this->pool.size());
		
		for(size_t i=1;i<this->pool.size()-1;i++) { // never adjust i=0 (T=1) or the max temperature
			sw[i] = log(this->pool[i].temperature - this->pool[i-1].temperature);
			
			if( swap_history[i].N>0 && swap_history[i+1].N>0 ) { // only adjust if there are samples
				sw[i] += k(this->pool[i].samples, v, t0) * (swap_history[i].mean()-swap_history[i+1].mean()); 
			}
			
		}
		
		// Reset all of the swap histories (otherwise we keep adapting in a bad way)
		// As of Jan 2023 we don't reset the histories since these are finite_histories 
		//for(size_t i=1;i<this->pool.size();i++) { 
		//	swap_history[i].reset();
		//	swap_history[i] << true; swap_history[i] << false; // this is a little +1 smoothing
		//	}
		
		// and then convert S to temperatures again
		// but never adjust i=0 (T=1) OR the last one
		for(size_t i=1;i<this->pool.size();i++) { 
			this->pool[i].temperature = this->pool[i-1].temperature + exp(sw[i]);
		}
	}
	
};

