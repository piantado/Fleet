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

	// NOTE: These used to be atomics?
    unsigned long nvisits;  // how many times have I visited each node?
    bool open; // am I still an available node?
	const double explore; 
	
	mutable std::mutex child_mutex; // for access in parallelTempering
	
	typename HYP::data_t* data;
    this_t* parent; // who is my parent?
	callback_t* callback; 
    HYP value;

//	StreamingStatistics statistics;
	double max;
    
    FullMCTSNode(this_t* par, HYP& v, callback_t& cb) : explore(par->explore), data(par->data), parent(par), callback(&cb), value(v), max(-infinity) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		
        initialize();	
    }
    
    FullMCTSNode(double ex, HYP& h0, typename HYP::data_t* d, callback_t& cb) : 
		explore(ex), data(d), parent(nullptr),  callback(&cb), value(h0), max(-infinity) {
        
		initialize();        
		
		if(not h0.get_value().is_null()) { CERR "# Warning, initializing MCTS root with a non-null node." ENDL; }
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	FullMCTSNode(const this_t& m) = delete;
	FullMCTSNode(this_t&& m) : explore(m.explore) {
		std::lock_guard guard1(m.child_mutex); // get m's lock before moving
		std::lock_guard guard3(  child_mutex); // and mine to be sure
		
		assert(m.children.size() ==0); // else parent pointers get messed up
		nvisits = m.nvisits;
		open = m.open;
		max = m.max;
//		statistics = std::move(statistics);
		parent = m.parent;
		value = std::move(m.value);
		children = std::move(children);
		callback = m.callback;
		data = m.data;		
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
		
//		double s = score(); // must call before getting stats mutex, since this requests it too 
		
		o << idnt TAB opn TAB max TAB "visits=" << nvisits TAB value.string() ENDL;
		
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
//		statistics << v;
		if(v > max) { max = v; }
		
		// and add this sampel going up too
		if(parent != nullptr) {
			parent->add_sample(v);
		}
    }
		
	void add_child_nodes() {

		std::lock_guard guard(child_mutex);
		
		if(children.size() == 0) { // check again in case someone else has edited in the meantime
			
			size_t N = value.neighbors();
//			std::cerr << N TAB value.string() ENDL;
						
			if(N==0) { // remove myself from the tree since these routes have been explored
				open = false;
			} 
			else {
				open = true;
				for(size_t ei = 0; ei<N; ei++) {
					size_t eitmp = ei; // since this is passed by refernece
					
					auto v = value.make_neighbor(eitmp);
					
					if(DEBUG_MCTS) DEBUG("\tAdding child ", this, "\t["+v.string()+"] ");
					
					//children.push_back(MCTSNode<HYP>(this,v));
					children.emplace_back(this,v,*callback);
				}
			}
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

   
    void search_one() {
       
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+value.string()+"] ", nvisits);
		
		// now if we get here, we must have at least one open child
		// so we can pick the best and recurse 
		
		++nvisits; // increment our visit count on our way down so other threads don't follow
		
		if(value.neighbors() == 0) {
			// if its a terminal, compute the posterior
			double posterior = value.compute_posterior(*data);
			//CERR posterior TAB value.string() ENDL;
			add_sample(posterior);
			(*callback)(value);
		}
		else {
			// else I have some neighbors, so I must have children. Add them if necessary
			if(children.size() == 0) { // if I have no children
				this->add_child_nodes(); // add child nodes if they don't exist
			}
			
			
			const std::function<double(const this_t&)> score = [&](const this_t& n) -> double { 
				if(not n.open) { // do not pick
					return -infinity;
				}
				else if(n.value.neighbors() == 0) {
					// terminals first
					return infinity;
				}
				else if(n.nvisits == 0) { 
					// take unexplored paths first
					return infinity; 
				}
				else {			
					// borrowing some UCT-bound-like stuff here
					// but really we should be thinking about the limiting distribution
					// how often do we explore each branch as the number of samples 
					// gets large?
					return n.max + explore * sqrt(log(nvisits)/n.nvisits);
				}	
			};
			
			auto c = max_of<this_t,std::vector<this_t>>(children, score);
			
			if(c.first == nullptr) return;
			
			c.first->search_one();
		}
		
		
		// and I'm open if ANY of my children are
		open = (open_children() > 0);
		
		// and erase closed children 
//		for(size_t i=children.size()-1;i>=0;i++) {
//			if(not children[i].open) {
//				children.erase(children.begin() + i);
//			}
//		}
		
    } // end search

};