#pragma once 

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
	std::vector<FiniteHistory<bool>> swap_history;
	std::atomic<bool> terminate; // used to kill swapper and adapter
	
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
	
	
	DataTempering(HYP& h0, HYP::data_t data, int n, double pct)  : terminate(false) {
		// allocates data views geometrically, removing pct at each next level  
		
		swap_history.reserve(n);
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
		std::thread swapper(&ParallelTempering<HYP>::__swapper_thread, this); // pass in the non-static mebers like this:
		
		#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
		std::thread shower(&ParallelTempering<HYP>::__shower_thread, this);
		#endif

		// run normal pool run
		for(auto& h : ChainPool<HYP>::run(ctl)) {
			co_yield h;
		}
		
		// kill everything else once that thread is done
		terminate = true;
		
		swapper.join();
		
		#ifdef PARALLEL_TEMPERING_SHOW_DETAIL
		shower.join();
		#endif

	}
	
	void adapt(double v=3, double t0=1000000) { assert(false && "*** You should not call adapt for DataTempering");}
};

