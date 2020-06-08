#pragma once

#include "IO.h"
#include "Control.h"
#include <queue>
#include <thread>

#include "FleetStatistics.h"
#include "ParallelInferenceInterface.h"

//#define DEBUG_ASTAR 1

/**
 * @class Astar
 * @author piantado
 * @date 07/06/20
 * @file Astar.h
 * @brief This is an implementation of A* search that maintains a priority queue of partial states and attempts to find 
 * 			a program with the lowest posterior score. To do this, we choose a node to expand based on its prior plus
 * 		    a single sample of its likelihood, computed by filling in its children at random. This stochastic heuristic is actually
 *  		inadmissable since it overestimtes the cost almost all of the time. As a result, it usually makes sense to run
 *  		A* at a pretty high temperature, corresponding to a downweighting of the likelihood.
 */
template<typename HYP, typename callback_t>
class Astar :public ParallelInferenceInterface {
	
	std::mutex lock; 
public:
	static constexpr size_t INITIAL_SIZE = 10000000;
	static constexpr size_t N_REPS = 1; // how many times do we try randomly filling in to determine priority? 
	static double temperature;	
	static callback_t* callback; // must be static so it can be accessed in GraphNode
	static HYP::data_t* data;
	
	/**
	 * @class GraphNode
	 * @author piantado
	 * @date 07/06/20
	 * @file Astar.h
	 * @brief A small class to store hypotheses and their priorities in our priority queue
	 */		
	struct GraphNode {
		HYP value;
		double prior;
		double likelihood;
		
		GraphNode(HYP& h, double p, double ll) : value(h), prior(p), likelihood(ll) {}
		
		double priority() const {
			return -(prior + likelihood/temperature);
		}
		
		bool operator>(const GraphNode& n) const {
			return priority() > n.priority();
		}	
	};
	
	// the smallest element appears on top of this vector
	std::priority_queue<GraphNode, ReservedVector<GraphNode,INITIAL_SIZE>, std::greater<GraphNode>> Q;
	
	 
	Astar(HYP& h0, HYP::data_t* d, callback_t& cb, double temp) {
		callback = &cb; // set these static members (static so GraphNode can use them without having so many copies)
		data = d;
		temperature = temp;
		push(h0, h0.compute_prior(), -infinity);
	}
	
	void push(HYP& h, double prior, double ll) {
		std::lock_guard guard(lock);
		Q.emplace(h,prior,ll);
	}
	
	void run_thread(Control ctl) override {

		ctl.start();
		while(ctl.running() and not Q.empty()) {
			
			lock.lock();
			if(Q.empty()) {
				lock.unlock();
				continue; // this is necesssary because we might have more threads than Q to start off
			}
			auto qt = Q.top(); Q.pop(); ++FleetStatistics::astar_steps;
			auto t = qt.value; 
			lock.unlock();
			
			#ifdef DEBUG_ASTAR
				CERR std::this_thread::get_id() TAB "ASTAR popped " << qt.priority() TAB t.string() ENDL;
			#endif
			
			size_t neigh = t.neighbors();
			assert(neigh>0); // we should not have put on things with no neighbors
			for(size_t k=0;k<neigh;k++) {
				auto v = t.make_neighbor(k);
				
				// if v is a terminal, we callback
				// otherwise we 
				if(v.is_evaluable()) {
					v.compute_posterior(*data); 
					(*callback)(v);
				}
				else {
					
					// here we add h, after computing its priority; note that we do this here and not in GraphNode so that 
					// when we multithread, we don't hold the lock while we copy_and_complete and call this posterior
					
					// We compute a stochastic heuristic by filling in h some number of times and taking the min
					// note since we are seeking the best *underestimate*, we can use qt's previous likelihood and 
					// see if we ever beat that (because we know this is a filling in of the previous likelihood)
					double likelihood = qt.likelihood;
					for(size_t i=0;i<N_REPS;i++) {
						auto g = v.copy_and_complete();
						g.compute_posterior(*data);
						(*callback)(g);
						likelihood = std::max(likelihood, g.likelihood);
					}
							
					push(v, v.compute_prior(), likelihood);
				}
			}
		}
	}
	
	
	
	
};

template<typename HYP, typename callback_t>
callback_t* Astar<HYP,callback_t>::callback = nullptr;

template<typename HYP, typename callback_t>
HYP::data_t* Astar<HYP,callback_t>::data = nullptr;

template<typename HYP, typename callback_t>
double Astar<HYP,callback_t>::temperature = 100.0;