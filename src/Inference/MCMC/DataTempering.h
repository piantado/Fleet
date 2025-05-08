#pragma once 


#define PARALLEL_TEMPERING_SHOW_DETAIL

#include <span>

#include "ParallelTempering.h"

/**
 * @class DataTempering
 * @author piantado
 * @date 10/06/20
 * @file ParallelTempering.h
 * @brief This is like ParallelTempering but it tempers on the amount of data. Note then it doesn't adapt anything. 
 * 		  NOTE also that our likelihood should be rescaled appropriately so that when we consider swaps, we aren't biased
 *        by the amount of data. This is not needed if the likelihood is computed with a mean
 */
template<typename HYP>
class DataTempering : public ParallelTempering<HYP> {
	
public:

	using Super = ParallelTempering<HYP>;
	
//	DataTempering(HYP& h0, std::vector<typename HYP::data_t>& datas)  : terminate(false) {
//		
//		swap_history.reserve(datas.size());
//		
//		// This version anneals on data, giving each chain a different amount in datas order
//		for(size_t i=0;i<datas.size();i++) {
//			this->pool.push_back(ChainPool<HYP>(i==0?h0:h0.restart(), &(datas[i])));
//			this->pool[i].temperature = 1.0;
//			swap_history.emplace_back();
//		}
//	}
	
	
	DataTempering(HYP& h0, HYP::data_t data, int n, double pct)  : Super() {
		// allocates data views geometrically, removing pct at each next level  
		
		this->swap_history.reserve(n);
		assert(pct > 0 and pct <= 1);
		
		// This version anneals geometriclly on data, giving each chain a different amount in datas order
		for(int i=0;i<n;i++) {
			int k = std::round(data.size() * pow(pct,i));
			if(k <= 0) {
				print("*** Warning, DataTempering ran out of data at i=", i, ". Try increasing pct or decreasing n.");
				break; 
			}
			print(i,k);
			
			auto chain = MCMCChain<HYP>(i==0?h0:h0.restart(), data.subspan(0,k));
			chain.temperature = 1.0;
			this->add_chain(chain);
//			this->pool.push_back(chain);
//			swap_history.emplace_back();
//			this->running.emplace_back();
		}
		
	}
	
	
	
	// Same as ParallelTempering, but no adapter
	generator<HYP&> run(Control ctl) {
		
		// Start a swapper and adapter thread
		std::thread swapper(&DataTempering<HYP>::__swapper_thread, this); // pass in the non-static mebers like this:
		
		#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
		std::thread shower(&DataTempering<HYP>::__shower_thread, this);
		#endif

		// run normal pool run
		for(auto& h : ChainPool<HYP>::run(ctl)) {
			co_yield h;
		}
		
		// kill everything else once that thread is done
		this->terminate = true;
		
		swapper.join();
		
		#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
		shower.join();
		#endif

	}
	
	// we need a new swapper since each has to be re-evaluated on the other data, since the data is different
	void __swapper_thread() {
			
		// runs a swapper every swap_every ms (double)
		// NOTE: If we have 1 element in the pool, it is caught below
		while(!(this->terminate or CTRL_C)) {
			
			// make this interruptible
			auto last = now();
			while(time_since(last) < this->swap_every and (not CTRL_C) and (not this->terminate))
				std::this_thread::sleep_for(std::chrono::milliseconds(this->time_resolution_ms)); 
			
			//print("SWAP", this->swap_history.size(), this->pool.size());
			assert(this->swap_history.size() == this->pool.size());
			
			// Here we are going to go through and swap each chain with its neighbor
			std::lock_guard og(this->overall_mutex); // must get this so we don't lock by another thread stealing one of below
			for(size_t k=this->pool.size()-1;k>=1;k--) { // swap k with k-1; starting at the bottom so stuff can percolate up
				if(CTRL_C or this->terminate) break; 
				
				// get both of these thread locks
				std::lock_guard guard1(this->pool[k-1].current_mutex);
				std::lock_guard guard2(this->pool[k  ].current_mutex);
				
				// compute R based on *data* swapping
				double Tnow = this->pool[k-1].getCurrent().likelihood + 
							  this->pool[k  ].getCurrent().likelihood;
				double Tswp = this->pool[k-1].getCurrent().compute_likelihood(this->pool[k].data) +
							  this->pool[k  ].getCurrent().compute_likelihood(this->pool[k-1].data);
				double R = Tswp-Tnow;
	
				//DEBUG("Swap p: ", k, R, this->pool[k-1].samples, this->pool[k-1].getCurrent().posterior, this->pool[k-1].getCurrent()); 
				
				if(R >= 0 or flip(exp(R))) { 
										
					#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
					COUT "# Data swapping " <<k<< " and " <<(k-1)<<"." TAB Tnow TAB Tswp ENDL;
					COUT "# " TAB this->pool[k].getCurrent().string() ENDL;
					COUT "# " TAB this->pool[k-1].getCurrent().string() ENDL;
					#endif
					
					// swap the chains
					std::swap(this->pool[k].getCurrent(), this->pool[k-1].getCurrent());
					
					// recompute posteriors (since the old was on other data)
					this->pool.at(k  ).getCurrent().compute_posterior(this->pool.at(k  ).data);
					this->pool.at(k-1).getCurrent().compute_posterior(this->pool.at(k-1).data);
					
					// and let's swap their steps since improvement so that the steps-since-improvement
					// stays with a chain 
					std::swap(this->pool[k].steps_since_improvement, this->pool[k-1].steps_since_improvement);

					this->swap_history.at(k) << true;
				}
				else {
					this->swap_history.at(k) << false;
				}
								
			}
		}
	}
	
	
	void adapt(double v=3, double t0=1000000) { assert(false && "*** You should not call adapt for DataTempering");}
};

