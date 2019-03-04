#pragma once 

#include "ChainPool.h"

template<typename HYP>
class ParallelTempering  {
	// follows https://arxiv.org/abs/1501.05823
	// This is a kind of Chain Pool that runs mutliple chains at different temperatures
	// and ajusts the temperatures in order to equate swaps up and down 
	
public:
	std::vector<MCMCChain<HYP>*> pool;
	std::vector<double> temperatures;
	FiniteHistory<bool>* swap_history;
	
	std::atomic<bool> terminate; // used to kill swapper and adapter
	
	ParallelTempering(HYP* h0, typename HYP::t_data* d, void(*cb)(HYP*), std::initializer_list<double> t) : temperatures(t), terminate(false) {
		for(size_t i=0;i<temperatures.size();i++) {
			pool.push_back(new MCMCChain<HYP>(i==0?h0:h0->restart(), d, cb));
			pool[i]->temperature = temperatures[i]; // set its temperature 
			
			swap_history = new FiniteHistory<bool>[temperatures.size()];
		}
	}
	
	
	ParallelTempering(HYP* h0, typename HYP::t_data* d, void(*cb)(HYP*), unsigned long n, double maxT) : terminate(false) {
		
		for(size_t i=0;i<n;i++) {
			
			pool.push_back(new MCMCChain<HYP>(i==0?h0:h0->restart(), d, cb));
			
			if(i==0) {  // always initialize i=0 to T=1s
				pool[i]->temperature = 1.0;
			}
			else {
				// set its temperature with this kind of geometric scale  
				pool[i]->temperature = 1.0 + (maxT-1.0) * pow(2.0, double(i)-double(n-1)); 
			}
			swap_history = new FiniteHistory<bool>[n];
		}
	}
	
	
	~ParallelTempering() {
		for(auto p : pool) 
			delete p;
		
		delete[] swap_history;
	}
	
	void __swapper_thread(unsigned long swap_every ) {
		// runs a swapper
		// swap_every is in ms
		
		while(!terminate) {
			std::this_thread::sleep_for(std::chrono::milliseconds(swap_every));
			
			size_t k = 1+myrandom(pool.size()-1); // swap k with k-1
			
			pool[k-1]->current_mutex.lock(); // wait for lock
			pool[k]->current_mutex.lock(); // wait for lock
			
			double Tnow = pool[k-1]->at_temperature(pool[k-1]->temperature)   + pool[k]->at_temperature(pool[k]->temperature);
			double Tswp = pool[k-1]->at_temperature(pool[k]->temperature)     + pool[k]->at_temperature(pool[k-1]->temperature);
			// TODO: Compare to paper
			if(Tswp > Tnow || uniform(rng) < exp(Tswp-Tnow)) { 
				
				// swap the chains
				std::swap(pool[k]->current, pool[k-1]->current);

				swap_history[k] << true;
			}
			else {
				swap_history[k] << false;
			}
			
			pool[k-1]->current_mutex.unlock(); 
			pool[k]->current_mutex.unlock(); 
			
		} // end while true
	}
	
	void __adapter_thread(unsigned long adapt_every) {
		while(!terminate) {
			std::this_thread::sleep_for(std::chrono::milliseconds(adapt_every));
			show_statistics();
			adapt(); // TOOD: Check what counts as t
		}
	}
	
	static void __run_helper(MCMCChain<HYP>* c, unsigned long steps, unsigned long time) {
		c->run(steps, time);
	}
	
	void run(unsigned long steps, unsigned long time, unsigned long swap_every, unsigned long adapt_every) {
		
		std::thread threads[pool.size()]; 
		
		// start everyone runnig 
		for(unsigned long i=0;i<pool.size();i++) {
			threads[i] = std::thread(__run_helper, pool[i], steps, time);
		}
		
		// pass in the non-static mebers like this:
		std::thread swapper(&ParallelTempering<HYP>::__swapper_thread, this, swap_every);
		std::thread adapter(&ParallelTempering<HYP>::__adapter_thread, this, adapt_every);

		for(unsigned long i=0;i<pool.size();i++) {
			threads[i].join();
		}
		
		terminate = true;
		swapper.join();
		adapter.join();
	}
	
	void show_statistics() {
		COUT "# Pool info: \n";
		for(size_t i=0;i<pool.size();i++) {
			COUT "# " << i TAB pool[i]->temperature TAB pool[i]->current->posterior TAB
					     pool[i]->acceptance_ratio() TAB swap_history[i].mean() TAB pool[i]->samples ENDL;
		}
	}
	
	double k(unsigned long t, double v, double t0) {
		return (1.0/v) * t0/(t+t0); 
	}
	
	void adapt(double v=3, double t0=1000000) {
		
		double S[pool.size()];
		
		for(size_t i=1;i<pool.size()-1;i++) { // never adjust i=0 (T=1) or the max temperature
			S[i] = log(pool[i]->temperature - pool[i-1]->temperature);
			
			if( swap_history[i].N>0 && swap_history[i+1].N>0 ) { // only adjust if there are samples
				S[i] += k(pool[i]->samples, v, t0) * (swap_history[i].mean()-swap_history[i+1].mean()); 
			}
			
		}
		
		// and then convert S to temperatures again
		for(size_t i=1;i<pool.size()-1;i++) { // never adjust i=0 (T=1)
			pool[i]->temperature = pool[i-1]->temperature + exp(S[i]);
		}
	}
	
};