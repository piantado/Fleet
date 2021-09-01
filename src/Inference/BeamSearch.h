#pragma once

#include "IO.h"
#include "Control.h"
#include <queue>
#include <thread>
#include <signal.h>

#include "FleetStatistics.h"
#include "ThreadedInferenceInterface.h"

//#define DEBUG_BEAMSEARCH 1

extern volatile sig_atomic_t CTRL_C; 

/**
 * @class BeamSearch
 * @author piantado
 * @date 07/06/20
 * @file BeamSearch.h
 * @brief This is an implementation of beam search that maintains a priority queue of partial states and attempts to find 
 * 			a program with the lowest posterior score. To do this, we choose a node to expand based on its prior plus
 * 		    N_REPS samples of its likelihood, computed by filling in its children at random. This stochastic heuristic is actually
 *  		inadmissable (in A* terms) since it usually overestimates the cost. As a result, it usually makes sense to run
 *  		at a pretty high temperature, corresponding to a downweighting of the likelihood, and making the heuristic more likely
 * 			to be admissable. 
 * 
 * 			One general challenge is how to handle -inf likelihoods, and here we've done that by, if you end up with -inf, taking
 * 		    your parent's temperature and multiplying by PARENT_PENALTY. 
 * 
 * 			TODO: Is it worth making this stochastic, so we sample the the expansion with the probabilities and some temperature?
 * 
 * 
 */
template<typename HYP>
class BeamSearch : public ThreadedInferenceInterface<HYP> {
	
	std::mutex lock; 
public:
	
	static constexpr size_t N = 1000000;  // how big is this?
	
	static constexpr size_t N_REPS = 10; // how many times do we try randomly filling in to determine priority? 
	static constexpr double PARENT_PENALTY = 1.1;
	static double temperature;	
	static typename HYP::data_t* data;
	
	// the smallest element appears on top of this vector
	TopN<HYP> Q;
	 
	BeamSearch(HYP& h0, typename HYP::data_t* d, double temp)  : Q(N) {
		
		// set these static members (static so GraphNode can use them without having so many copies)
		data = d;
		temperature = temp;
		
		// We're going to make sure we don't start on -inf because this value will
		// get inherited by my kisd for when they use -inf
		for(size_t i=0;i<1000 and !CTRL_C;i++) {
			auto g = h0;
			g.complete();
			
			g.compute_posterior(*data);
			
			if(g.likelihood > -infinity) {
				h0.prior = 0.0;
				h0.likelihood = g.likelihood / temperature;				
				add(h0);
				break;
			}
		}
		
		assert(not Q.empty() && "*** You should have pushed a non -inf value into Q above -- are you sure its possible?");
	}
	
	void add(HYP& h)  { Q.add(h); }	
	void add(HYP&& h) { Q.add(std::move(h)); }
	
	generator<HYP&> run_thread(Control& ctl) override {

		ctl.start();
		while(ctl.running()) {
			
			lock.lock();
			if(Q.empty()) {
				lock.unlock();
				continue; // this is necesssary because we might have more threads than Q to start off. 
			}
			auto t = Q.best(); Q.pop(); ++FleetStatistics::beam_steps;
			lock.unlock();
			
			#ifdef DEBUG_BEAMSEARCH
				CERR std::this_thread::get_id() TAB "BeamSearch popped " << t.posterior TAB t.string() ENDL;
			#endif
			
			size_t neigh = t.neighbors();
			assert(neigh>0 && "*** Your starting state has zero neighbors -- did you remember to give it a LOTHypothesis with an empty value?"); // we should not have put on things with no neighbors
			for(size_t k=0;k<neigh and ctl.running();k++) {
				auto v = t.make_neighbor(k);
				
				// if v is a terminal, we co_yield
				// otherwise we 
				if(v.is_evaluable()) {
					
					v.compute_posterior(*data); 
					
					co_yield v;
				}
				else {
					//CERR v.string() ENDL;
					
					// We compute a stochastic heuristic by filling in h some number of times and taking the min
					// we're going to start with the previous likelihood we found -- NOTE That this isn't an admissable
					// heuristic, and may not even make sense, but seems to work well, meainly in preventing -inf
					double likelihood = t.likelihood * PARENT_PENALTY;  // remember this likelihood is divided by temperature
					for(size_t i=0;i<N_REPS and not CTRL_C;i++) {
						auto g = v; g.complete();
						g.compute_posterior(*data);
						
						co_yield g; 
						
						// this assigns a likelihood as the max of the samples
						if(not std::isnan(g.likelihood)) {
							likelihood = std::max(likelihood, g.likelihood / temperature); // temperature must go here
						}
					}
					 
					// now we save this prior and likelihood to v (even though v is a partial tree)
					// so that it is sorted (defaultly by posterior) 
					v.likelihood = likelihood; // save so we can use next time
					v.posterior  = v.compute_prior() + v.likelihood;
					//CERR "POST:" TAB v.string() TAB v.posterior TAB likelihood ENDL;
					
					add(std::move(v));
				}
			}
			
			// this condition can't go at the top because of multithreading -- we need each thread to loop in the top when it's
			// waiting at first for the Q to fill up
			if(Q.empty()) break; 
		}
	}
	
	
	
	
};


template<typename HYP>
typename HYP::data_t* BeamSearch<HYP>::data = nullptr;

template<typename HYP>
double BeamSearch<HYP>::temperature = 100.0;