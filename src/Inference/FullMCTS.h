/* 
 * 
 * NOTE: If we scored inf, then we don't want to use that next time...
 * 
 * 
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
class FullMCTSNode {	
public:

	using this_t = FullMCTSNode<HYP,callback_t>;
	using data_t = typename HYP::data_t;
	
	std::vector<this_t> children;

    unsigned int nvisits;  // how many times have I visited each node?
    bool open; // am I still an available node?
	
	SpinLock mylock; 
	
	int which_expansion; 
	
	// these are static variables that makes them not have to be stored in each node
	// this means that if we want to change them for multiple MCTS nodes, we need to subclass
	static double explore; 
	static data_t* data;
	static callback_t* callback; 

	float max;
	float min;
	float lse;
	float last_lp;
    
	FullMCTSNode() {
	}
	
    FullMCTSNode(HYP& start, this_t* par,  size_t w) : 
		nvisits(0), open(true), which_expansion(w), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		mylock.lock();
			
		children.reserve(start.neighbors());
		mylock.unlock();
    }
    
    FullMCTSNode(HYP& start, double ex, data_t* d, callback_t& cb) : 
		nvisits(0), which_expansion(0), open(true), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
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

	
	size_t any_open() const {
		for(auto const& c : children) {
			if(c.open) return true;
		}
		return false;
	}

    void add_sample(const float v) {
        max = std::max(max,v);	
		if(v != -infinity) // keep track of the smallest non-inf value -- we use this instead of inf for sampling
			min = std::min(min, v); 
		lse = logplusexp(lse, v);
		last_lp = v;
    }
		
   
	// search for some number of steps
	void search(Control ctl, HYP& h0) {
		// NOTE: This is important for multithreading that each top-level search call here makes a *copy*
		// ctl controls the mcts path through nodes
		assert(ctl.threads == 1);
		ctl.start();
		
		while(ctl.running()) {
			if(DEBUG_MCTS) DEBUG("\tMCTS SEARCH LOOP");
			
			HYP current = h0; // each thread makes a real copy here 
			double s = this->search_one(this, current); 
			add_sample(s);
		}
	}

	
	void parallel_search(Control ctl, HYP& h0) { 
		
		auto __helper = [&](FullMCTSNode<HYP,callback_t>* h, Control ctl) {
			h->search(ctl, h0);
		};
		
		std::vector<std::thread> threads(ctl.threads); 
		
		// start everyone running 
		for(unsigned long i=0;i<ctl.threads;i++) {
			Control ctl2 = ctl; ctl2.threads=1;
			threads[i] = std::thread(__helper, this, ctl2);
		}
		
		for(unsigned long i=0;i<ctl.threads;i++) {
			threads[i].join();
		}	
	}

   
//	double score(const this_t* parent, const HYP& current) {
//		mylock.lock();
//		
//		if(not open) { // do not pick
//			mylock.unlock();
//			return -infinity;
//		}
//		else if(current.neighbors() == 0) {
//			mylock.unlock();
//			// terminals first
//			return infinity;
//		}
//		else if(nvisits == 0) { 
//			mylock.unlock();
//			// take unexplored paths first
//			return infinity; 
//		}
//		else {			
//			
//			if(parent == nullptr) {
//				mylock.unlock();
//				return 0.0;
//			}
//			
//			// min/last thing with UCT -- this prevents the -infs from messing things up 
////			return  ((last_lp == -infinity ? min : last_lp) - parent->lse) + 
////			         explore * sqrt(log(parent->nvisits)/nvisits);
//			
////			CERR last_lp TAB parent->lse ENDL;
//			
//			double v = explore * sqrt(log(parent->nvisits)/(1+nvisits)); // exploration part
//			
//			if( parent->lse == -infinity ) {
//				// This special case is weird, we'll just go with the explore parameter
//			}
//			else {
//				// this is our measure of how well each branch does, subbing last_lp for infinity
//				// when its helpful
//				v += exp((last_lp == -infinity and min < infinity ? min : last_lp) - parent->lse);
//			}
//			
//			assert(!std::isnan(v));
//			assert(v != infinity);
//			mylock.unlock();
//			return v; 
//			
////			return  exp((last_lp == -infinity ? min : last_lp) - parent->lse) + 
////			         explore * sqrt(log(parent->nvisits)/nvisits);
//			
////			return  (lse - parent->lse) + 
////					explore * sqrt(log(parent->nvisits)/nvisits);
//			
//		}	
//	}
   
    double get_ll() {
		if(last_lp != -infinity) {
			return last_lp;
		}
		else if(min != infinity) {
			return min;
		} 
		else {
			return -infinity;
		}
	}
   
   
    double search_one(this_t* parent, HYP& current) {
		// recurse down the tree, building and/or evaluating as needed. This has a nice format that saves space -- Nodes need
		// not store parents since they can be passed recursively here. Because this returns the score, it can be propagated
		// back up the tree. 
		
		mylock.lock();
			
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+current.string()+"] ", nvisits);
		
		// now if we get here, we must have at least one open child
		// so we can pick the best and recurse 
		
		++nvisits; // increment our visit count on our way down so other threads don't follow
		
		auto neigh = current.neighbors(); 
		
		if(neigh == 0) {
			// if I am a terminal 
			
			open = false; // make sure nobody else takes this one
			
			// if its a terminal, compute the posterior
			current.compute_posterior(*data);
			add_sample(current.likelihood);
			(*callback)(current);
			
			mylock.unlock();
			
			return current.likelihood;
		}
		else {
			
			// Fill up the child nodes
			// TODO: This is inefficient because it copies current a bunch of times...
			if(children.size() == 0) {
				for(size_t k=0;k<neigh;k++) {
					HYP kc = current; kc.expand_to_neighbor(k);
					children.emplace_back(kc, this, k);
				}
			}
			
			
			
			
			
			
			
			
			
			// Hmm what if on construction you inherit min from your parent?
			
			// Change to include UCT part of bound
			// OR just do temperature?
			
			// Change so not all kids are added...
			double temp = 100.0;
			
			
			// sample from children, using last_ll as the probability for missing children
			std::vector<double> children_lps(neigh);
			for(size_t k=0;k<neigh;k++) {
				if(children[k].open) {
					children_lps[k] = (current.neighbor_prior(k) + 
									  (children[k].nvisits == 0 ? last_lp : children[k].last_lp ) / temp);
									//  (children[k].nvisits == 0 ? get_ll() : children[k].get_ll());
									//(children[k].last_lp==-infinity ? last_lp : children[k].last_lp);
				}
				else {
					children_lps[k] = -infinity;
				}
			}
			
			// this is an index into children
			int idx = sample_int_lp(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
			
			current.expand_to_neighbor(idx); // idx here gives which expansion we follow
			
			if(DEBUG_MCTS) DEBUG("\tAdding child ", this, "\t["+current.string()+"] ");

			// and now follow this newly added child
			mylock.unlock();

			double s = children[idx].search_one(parent, current);
			
			mylock.lock();
				add_sample(s);
			mylock.unlock();
			
			return s;
		}
    } // end search

};

// Must be defined for the linker to find them, apparently:
template<typename HYP, typename callback_t>
double FullMCTSNode<HYP,callback_t>::explore = 1.0;

template<typename HYP, typename callback_t>
typename HYP::data_t* FullMCTSNode<HYP,callback_t>::data = nullptr;

template<typename HYP, typename callback_t>
callback_t* FullMCTSNode<HYP,callback_t>::callback = nullptr;