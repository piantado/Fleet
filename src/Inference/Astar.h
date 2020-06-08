#pragma once

#include "IO.h"
#include "Control.h"
#include <queue>
#include <thread>

#include "FleetStatistics.h"

//#define DEBUG_ASTAR 1

/**
 * @class Astar
 * @author piantado
 * @date 07/06/20
 * @file Astar.h
 * @brief This is an implementation of A* search that maintains a priority queue of partial states and attempts to find 
 * 			a program with the lowest posterior score. To do this, we choose a node to expand based on its prior plus
 * 		    a single sample of its likelihood, computed by filling in its children at random. This is a stochastic heuristic
 * 			but still oen guaranteed not to *over*-estimate the posterior score. 
 */
template<typename HYP, typename callback_t>
class Astar {
public:
	static constexpr size_t INITIAL_SIZE = 10000000;
	
	static constexpr double temperature = 50.0;
	std::mutex lock; 
	
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
		double priority; 
		
		GraphNode(HYP& h, double pri) : value(h), priority(pri) {}
		
		bool operator>(const GraphNode& n) const {
			return priority > n.priority;
		}	
	};
	
	// the smallest element appears on top of this vector
	std::priority_queue<GraphNode, ReservedVector<GraphNode,INITIAL_SIZE>, std::greater<GraphNode>> Q;
	
	 
	Astar(HYP& h0, HYP::data_t* d, callback_t& cb) {
		callback = &cb; // set these static members (static so GraphNode can use them without having so many copies)
		data = d;
		push(h0);
	}
	
	void push(HYP& h) {
	
		// here we add h, after computing its priority; note that we do this here and not in GraphNode so that 
		// when we multithread, we don't hold the lock while we copy_and_complete and call this posterior
		
		// We compute a stochastic heuristic by filling in h
		auto g = h.copy_and_complete();
		g.compute_posterior(*data);
		(*callback)(g);
		
		// important here, the priority is the prior on h and the likelihood from g
		// (you don't want the prior on g because it might be much more)
		double priority = -(h.compute_prior() + g.likelihood / temperature);
		
		std::lock_guard guard(lock);
		Q.emplace(h,priority);
	}
	
	void __run_helper(Control ctl) {

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
				CERR std::this_thread::get_id() TAB "ASTAR popped " << qt.priority TAB t.string() ENDL;
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
					push(v);
				}
			}
		}
	}
	
	
	virtual void run(Control ctl) {
		
		std::vector<std::thread> threads(ctl.threads); 

		for(unsigned long t=0;t<ctl.threads;t++) {
			Control ctl2 = ctl; ctl2.threads=1; // we'll make each thread just one
			threads[t] = std::thread(&Astar<HYP,callback_t>::__run_helper, this, ctl2);
		}
		
		// wait for all to complete
		for(unsigned long t=0;t<ctl.threads;t++) {
			threads[t].join();
		}
	}
	
	
	
};

template<typename HYP, typename callback_t>
callback_t* Astar<HYP,callback_t>::callback = nullptr;

template<typename HYP, typename callback_t>
HYP::data_t* Astar<HYP,callback_t>::data = nullptr;