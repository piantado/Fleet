/* 
 * 
 * Some ideas:
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

template<typename HYP, typename callback_t>
class FullMCTSNode {	
public:

	using this_t = FullMCTSNode<HYP,callback_t>;
	
    	
	std::vector<this_t> children;
	
	HYP value;

	// NOTE: These used to be atomics?
    unsigned long nvisits;  // how many times have I visited each node?
    bool open; // am I still an available node?
	mutable std::mutex child_mutex; // for access in parallelTempering
	this_t* parent; // who is my parent?
	
	
	// TODO: MAKE THESE STATIC 
	// these are static variables that makes them not have to be stored in each node
	// this means that if we want to change them for multiple MCTS nodes, we need to subclass
	double explore; 
	typename HYP::data_t* data;
	callback_t* callback; 
    
	/*	One idea here is that we want to pick the node whose *most recent* sample added the most probability mass
	 * 
	 * 
	 * */
	double max;
	double min;
	double lse;
	double last_lp;
    
    FullMCTSNode(this_t* par, HYP& v, callback_t& cb) : explore(par->explore), data(par->data), parent(par), callback(&cb), max(-infinity), min(infinity), lse(-infinity), last_lp(0) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		std::lock_guard guard(child_mutex);
		value = v;
		
		children.reserve(value.neighbors());
        initialize();	
    }
    
    FullMCTSNode(double ex, HYP& h0, typename HYP::data_t* d, callback_t& cb) : 
		explore(ex), data(d), parent(nullptr),  callback(&cb), max(-infinity), min(infinity), lse(-infinity), last_lp(0) {
        std::lock_guard guard(child_mutex);
		
		value = h0; // might need to be after lock_guard?
		children.reserve(value.neighbors());
		
		initialize();        
		
		if(not h0.get_value().is_null()) { CERR "# Warning, initializing MCTS root with a non-null node." ENDL; }
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	FullMCTSNode(const this_t& m) = delete; // because what happens to the child pointers?
	
	
	FullMCTSNode(this_t&& m) {
		// This must be defined for us to emplace_Back, but we don't actually want to move because that messes with 
		// the multithreading. So we'll throw an exception. You can avoid moves by reserving children up above, 
		// which should make it so that we never call this
		
		throw YouShouldNotBeHereError("*** This must be defined for children.emplace_back, but should never be called");
	}
		
    size_t size() const {
        int n = 1;
        for(const auto& c : children)
            n += c.size();
        return n;
    }
    
    void initialize() {
		nvisits=0;
        open=true;
    }
    
	void print(std::ostream& o, const int depth, const bool sort) const { 
        std::string idnt = std::string(depth, '\t'); // how far should we indent?
        
		std::string opn = (open?" ":"*");
		
		o << idnt TAB opn TAB score() TAB "visits=" << nvisits TAB value.string() ENDL;
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array because otherwise its not const			
			std::vector<const FullMCTSNode*> c2;
			for(const auto& c : children) c2.push_back(&c);
			std::sort(c2.begin(), c2.end(), [](const auto a, const auto b) {return a->nvisits > b->nvisits;}); // sort by how many samples

			for(const auto& c : c2) 
				c->print(o, depth+1, sort);
		}
		else {		
			for(auto& c : children) 
				c.print(o, depth+1, sort);
		}
    }
 
	// wrappers for file io
    void print(const bool sort=true) const { 
		print(std::cout, 0, sort);		
	}
	void printerr(const bool sort=true) const {
		print(std::cerr, 0);
	}    
	void print(const char* filename, const bool sort=true) const {
		std::ofstream out(filename);
		print(out, 0, sort);
		out.close();		
	}

	
	size_t open_children() const {
		size_t n = 0;
		for(auto const& c : children) {
			n += c.open;
		}
		return n;
	}



    void add_sample(const double v){ // found something with v
        
		// If we do the first of these, we sampling probabilities proportional to the probabilities
		// otherwise we sample them uniformly
		max = std::max(max,v);	
		if(v != -infinity) // keep track of the smallest non-inf value -- we use this instead of inf for sampling
			min = std::min(min, v); 
		lse = logplusexp(lse, v);
		last_lp = v;
		
		// and add this sampel going up too
		if(parent != nullptr) {
			parent->add_sample(v);
		}
    }
		
   
	// search for some number of steps
	void search(Control ctl) {
		// ctl controls the mcts path through nodes
		assert(ctl.threads == 1);
		ctl.start();
		
		while(ctl.running()) {
			if(DEBUG_MCTS) DEBUG("\tMCTS SEARCH LOOP");
			
			this->search_one();
		}
	}

	
	void parallel_search(Control ctl) { 
		
		auto __helper = [](FullMCTSNode<HYP,callback_t>* h, Control ctl) {
			h->search(ctl);
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

   
	double score() const {
		if(not open) { // do not pick
			return -infinity;
		}
		else if(value.neighbors() == 0) {
			// terminals first
			return infinity;
		}
		else if(nvisits == 0) { 
			// take unexplored paths first
			return infinity; 
		}
		else {			
			
			// min/last thing with UCT -- this prevents the -infs from messing things up 
			return  (last_lp == -infinity ? min-lse : last_lp-lse) + 
			        (parent == nullptr ? 0.0 : explore * sqrt(log(parent->nvisits)/nvisits));
			
		}	
	}
   
    void search_one() {
		// This new version of search_one is a little fancy in that it creates the nodes as it goes down. 
		//std::lock_guard guard(child_mutex); // need a guard here so the child isn't moved while we do this 
		child_mutex.lock();
			
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+value.string()+"] ", nvisits);
		
		// now if we get here, we must have at least one open child
		// so we can pick the best and recurse 
		
		++nvisits; // increment our visit count on our way down so other threads don't follow
		
		auto neigh = value.neighbors(); 
		
		if(neigh == 0) {
			open = false; // make sure nobody else takes this one
			
			// if its a terminal, compute the posterior
			double posterior = value.compute_posterior(*data);
			add_sample(posterior);
			(*callback)(value);
			
			child_mutex.unlock();
		}
		else if (children.size() < neigh) {
			// if I haven't expanded all of my children, do that first 
			
			{
//				std::lock_guard guard(child_mutex);
						
				auto child = value.make_neighbor(children.size());
				if(DEBUG_MCTS) DEBUG("\tAdding child ", this, "\t["+child.string()+"] ");
				
				children.emplace_back(this,child,*callback);
			}
			
			// and now follow this newly added child
			child_mutex.unlock();
			children.rbegin()->search_one();
			
		}
		else {
//			std::lock_guard guard(child_mutex);
			
			// else I have all of my kids
			//CERR children.size() TAB neigh ENDL;
			assert(children.size() == neigh); // I should have this many neighbors, so I'll choose

			// Figure out which to sample
			auto c = arg_max<this_t,std::vector<this_t>>(children, [](const this_t& n) {return n.score(); });
			
			// TODO: And if we track things right, we can remove children who are closed
			// but to do this, we have to store that we had seen all of them. 
			child_mutex.unlock();
			
			children[c.first].search_one();
			
			// and I'm open if ANY of my children are
			open = (open_children() > 0);
		}
		
		
    } // end search

};