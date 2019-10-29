#pragma once

#include <thread>

template<typename HYP, typename callback_t>
class ChainPool { 
	// Run a pool in parallel, one thread each 
public:
	std::vector<MCMCChain<HYP,callback_t>> pool;
	
	ChainPool() { }
	
	ChainPool(HYP& h0, typename HYP::t_data* d, callback_t& cb, size_t n, bool allcallback=true) {
		for(size_t i=0;i<n;i++) {
			if(allcallback or i==0) pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d, cb));
			else                    pool.push_back(MCMCChain<HYP,callback_t>(i==0?h0:h0.restart(), d));
		}
	}
	

	static void __run_helper(MCMCChain<HYP,callback_t>* c, unsigned long steps, unsigned long time) {
		c->run(steps, time);
	}
	
	virtual void run(unsigned long steps, unsigned long time) {
		
		std::thread threads[pool.size()]; 
		
		for(unsigned long i=0;i<pool.size();i++) {
			threads[i] = std::thread(this->__run_helper, &pool[i], steps, time);
		}
		
		// wait for all to complete
		for(unsigned long i=0;i<pool.size();i++) {
			threads[i].join();
		}
	}
	
	
};