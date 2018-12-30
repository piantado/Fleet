#pragma once

#include <thread>

template<typename HYP>
class ChainPool { 
	// Run a pool in parallel, one thread each 
public:
	std::vector<MCMCChain<HYP>*> pool;
	
	ChainPool(HYP* h0, typename HYP::t_data* d, void(*cb)(HYP*), size_t n) {
		for(size_t i=0;i<n;i++) {
			pool.push_back(new MCMCChain<HYP>(i==0?h0:h0->restart(), d, cb));
		}
	}
	
	~ChainPool() {
		for(auto v : pool) delete v;
	}

	static void __run_helper(MCMCChain<HYP>* c, unsigned long steps, unsigned long time) {
		c->run(steps, time);
	}
	
	void run(unsigned long steps, unsigned long time) {
		
		std::thread threads[pool.size()]; 
		
		for(unsigned long i=0;i<pool.size();i++) {
			threads[i] = std::thread(__run_helper, pool[i], steps, time);
		}
		
		// wait for all to complete
		for(unsigned long i=0;i<pool.size();i++) {
			threads[i].join();
		}
	}
	
	
};