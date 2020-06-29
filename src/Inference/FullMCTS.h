/* 

 * Some ideas:
 * 
 * 	For a node, we stor ethe last *likelihood*. We then sample the next child with the current prior and the last likelihood. 
 * 
 * 	what if we preferentially follow leaves that have a *variable* likelihood distribution. Otherwise we might find something good 
 *  and then follow up on minor modifications of it? This would automatically be handled by doing some kind of UCT/percentile estimate
 * 	
 *	Maybe if we estimate quantiles rather than means or maxes, this will take care of this? If you tak ethe 90th or 99th quantile, 
 *  then you'll naturally start with 
 * 
 *  We should be counting gaps against hypotheses in the selection -- the gaps really make us bound the prior, since each gap must be filled. 
 * 
 * TODO: Should be using FAME NO OS as in the paper
 * 
 * 
 * 	Another idea: what if we sample from the bottom of the tree according to probability? To do that we sample and then sum up the lses (excluding closed nodes)
 * 		and then recurse down until we find what we need
 * 
 * 		-- Strange if we just do the A* version, then we'll have a priority queue and that's a lot liek sampling from the leaves? 
 * 
 * */
#pragma once 

#define DEBUG_MCTS 0

#include <atomic>
#include <mutex>
#include <set>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <functional>

#include "StreamingStatistics.h"
#include "ParallelInferenceInterface.h"
#include "Control.h"

/**
 * @class FullMCTSNode
 * @author piantado
 * @date 01/03/20
 * @file MCTS.h
 * @brief Template of type hypothesis and callback.
 */


class SpinLock {
	// We need something much smaller than std::mutex
	
	//https://stackoverflow.com/questions/26583433/c11-implementation-of-spinlock-using-atomic
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) { ; }
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
};


/**
 * @class FullMCTSNode
 * @author piantado
 * @date 03/06/20
 * @file FullMCTS.h
 * @brief This monte care tree search always plays out a full tree. It is optimized to keep the tree as small as possible. 
 * 		To do this, it doesn't even store the value (HYP) in the tree, it just constructs it as a search step goes down the tree. 
 * 	    It also doesn't store parent pointers, it just passes them down the tree. We use SpinLock here because std::mutex is much
 *      bigger. Finally, explore, data, and callback are all static variables, which means that you will need to subclass this
 *      if you want to let those vary across nodes (or parallel runs). 
 * 		This lets us cram everything into 56 bytes. 
 * 
 * 		NOTE we could optimize a few more things -- for instance, storing the open bool either in nvisits, or which_expansion
 * 
 * 		TODO: We also store terminals currently in the tree, but there's no need for that -- we should eliminate that. 
 */

template<typename HYP, typename callback_t>
class FullMCTSNode : public ParallelInferenceInterface<HYP> {	
public:

	using this_t = FullMCTSNode<HYP,callback_t>;
	using data_t = typename HYP::data_t;
	
	std::vector<this_t> children;

   bool open; // am I still an available node?
	
	SpinLock mylock; 
	
	int which_expansion; 
	
	// these are static variables that makes them not have to be stored in each node
	// this means that if we want to change them for multiple MCTS nodes, we need to subclass
	static double explore; 
	static data_t* data;
	static callback_t* callback; 

	unsigned int nvisits;  // how many times have I visited each node?
	this_t* parent;
	float max;
	float min;
	float lse;
	float last_lp;
    
	FullMCTSNode() {
	}
	
    FullMCTSNode(HYP& start, this_t* par,  size_t w) : 
		open(true), which_expansion(w), nvisits(0), parent(par), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		mylock.lock();
			
		children.reserve(start.neighbors());
		mylock.unlock();
    }
    
    FullMCTSNode(HYP& start, double ex, data_t* d, callback_t& cb) : 
		open(true), which_expansion(0), nvisits(0), parent(nullptr), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// This is the constructor that gets called from main, and it sets the static variables. All the other constructors
		// should use the above one
		
        mylock.lock();
		
		children.reserve(start.neighbors());
		explore = ex; 
		data = d;
		callback = &cb;
		
		mylock.unlock();
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	FullMCTSNode(const this_t& m) = delete; // because what happens to the child pointers?
	
	
	FullMCTSNode(this_t&& m) {
		// This must be defined for us to emplace_Back, but we don't actually want to move because that messes with 
		// the multithreading. So we'll throw an exception. You can avoid moves by reserving children up above, 
		// which should make it so that we never call this		
		throw YouShouldNotBeHereError("*** This must be defined for children.emplace_back, but should never be called");
	}
		
	void operator=(const FullMCTSNode& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
	void operator=(FullMCTSNode&& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
		
		
    size_t size() const {
        int n = 1; // for me
        for(const auto& c : children)
            n += c.size();
        return n;
    }
    
    
	void print(HYP from, this_t* parent, std::ostream& o, const int depth, const bool sort) { 
		// here from is not a reference since we want to copy when we recurse 
		
        std::string idnt = std::string(depth, '\t'); // how far should we indent?
        
		std::string opn = (open?" ":"*");
		
		o << idnt TAB opn TAB last_lp TAB max TAB "visits=" << nvisits TAB which_expansion TAB from ENDL;
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array because otherwise its not const			
			std::vector<std::pair<FullMCTSNode*,int>> c2;
			int w = 0;
			for(auto& c : children) {
				c2.emplace_back(&c,w);
				++w;
			}
			std::sort(c2.begin(), 
					  c2.end(), 
					  [](const auto a, const auto b) {
							return a.first->nvisits > b.first->nvisits;
					  }
					  ); // sort by how many samples

			for(auto& c : c2) {
				HYP newfrom = from; newfrom.expand_to_neighbor(c.second);
				c.first->print(newfrom, const_cast<this_t*>(this), o, depth+1, sort);
			}
		}
		else {		
			int w = 0;
			for(auto& c : children) {
				HYP newfrom = from; newfrom.expand_to_neighbor(w);
				c.print(newfrom, const_cast<this_t*>(this), o, depth+1, sort);
				++w;;
			}
		}
    }
 
	// wrappers for file io
    void print(HYP& start, const bool sort=true) { 
		print(start, nullptr, std::cout, 0, sort);		
	}

	void print(HYP& start, const char* filename, const bool sort=true) {
		std::ofstream out(filename);
		print(start, nullptr, out, 0, sort);
		out.close();		
	}

    void add_sample(const float v) {
        max = std::max(max,v);	
		if(v != -infinity) // keep track of the smallest non-inf value -- we use this instead of inf for sampling
			min = std::min(min, v); 
		lse = logplusexp(lse, v);
		last_lp = v;
		
		// and propagate up the tree
		if(parent != nullptr) {
			parent->add_sample(v);
		}
		
    }
	
	virtual void run_thread(Control ctl, HYP h0) override {
		
		ctl.start();
		
		while(ctl.running()) {
			if(DEBUG_MCTS) DEBUG("\tMCTS SEARCH LOOP");
			
			HYP current = h0; // each thread makes a real copy here 
			this->search_one(current); 
		}		
	}
	

	/// Now we define some actions that happen when we make it to different kinds of nodes. These can be 
	/// overwritten by subclasses of MCTS	
	
	// What we do on a terminal node
	virtual void process_terminal(HYP& current) {
		open = false; // make sure nobody else takes this one
			
		// if its a terminal, compute the posterior
		current.compute_posterior(*data);
		add_sample(current.likelihood);
		(*callback)(current);
	}

	virtual void process_incomplete(HYP& current) {
		int neigh = current.neighbors(); 
		
		mylock.lock();
		
		// Fill up the child nodes
		// TODO: This is a bit inefficient because it copies current a bunch of times...
		// this is intentionally a while loop in case another thread has snuck in and added
		while(children.size() < (size_t)neigh) {
			int k = children.size(); // which to expand
			HYP kc = current; kc.expand_to_neighbor(k);
			children.emplace_back(kc, this, k);
		}
		
		mylock.unlock();
		
		// then treat this just like an internal node
		process_internal(current); 
	}

		
	virtual void process_internal(HYP& current) {
		int neigh = current.neighbors(); 
		// sample from children, using last_ll as the probability for missing children
		std::vector<double> children_lps(neigh, -infinity);
		for(int k=0;k<neigh;k++) {
			if(children[k].open){
				/// how much probability mass PER sample came from each child, dividing by explore for the temperature.
				/// If no exploraiton steps, we just pretend lse-1.0 was the probability mass 
				children_lps[k] = current.neighbor_prior(k) + 
								  (children[k].nvisits == 0 ? lse-1.0 : children[k].lse-log(children[k].nvisits)) / explore;
			}
		}
		
		// choose an index into children
		int idx = sample_int_lp(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
		
		// expand 
		current.expand_to_neighbor(idx); // idx here gives which expansion we follow
		
		// and recurse down
		children[idx].search_one(current);
	}


    void search_one(HYP& current) {
		// recurse down the tree, building and/or evaluating as needed. This has a nice format that saves space -- Nodes need
		// not store parents since they can be passed recursively here. Because this returns the score, it can be propagated
		// back up the tree. 
		
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+current.string()+"] ", nvisits);
		
		++nvisits; // increment our visit count on our way down so other threads don't follow
		
		int neigh = current.neighbors(); 
		
		// dispatch our sub functions depending on what kind of thing we are
		if(neigh == 0) 					            { process_terminal(current);  }
		else if (children.size() != (size_t)neigh)  { process_incomplete(current); }
		else 							            { process_internal(current);  } // otherwise a normal internal node
		
    } // end search

};

// Must be defined for the linker to find them, apparently:
template<typename HYP, typename callback_t>
double FullMCTSNode<HYP,callback_t>::explore = 1.0;

template<typename HYP, typename callback_t>
typename HYP::data_t* FullMCTSNode<HYP,callback_t>::data = nullptr;

template<typename HYP, typename callback_t>
callback_t* FullMCTSNode<HYP,callback_t>::callback = nullptr;



// A class that calls some number of playouts instead of building the full tree
//template<typename HYP, typename callback_t>
//class PartialMCTS : public FullMCTSNode<HYP,callback_t> {	
//	using Super = FullMCTSNode<HYP,callback_t>;
//	using Super::Super; // get constructors
//	
//	// Here we choose the max according a UCT-like bound
//	virtual void process_internal(HYP& current) {
//		int neigh = current.neighbors(); 
//		// sample from children, using last_ll as the probability for missing children
//		std::vector<double> children_lps(neigh, -infinity);		
//		for(int k=0;k<neigh;k++) {
//			if(children[k].open){
//				children_lps[k] = current.neighbor_prior(k) + 
//								  (children[k].nvisits == 0 ? 0.0 : children[k].max + explore*sqrt(log(nvisits)/children[k].nvisits));
//			}
//		}
//		
//		// pick the best
//		int idx = arg_max(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
//		current.expand_to_neighbor(idx); // idx here gives which expansion we follow
//		children[idx].search_one(current);
//	}
//	
//	// Here we 
//};