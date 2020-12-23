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
#include "SpinLock.h"
#include "Random.h"
#include "FleetArgs.h"
#include "PriorInference.h"

#include "BaseNode.h"



/**
 * @class MCTSBase
 * @author piantado
 * @date 03/06/20
 * @file FullMCTS.h
 * @brief This monte care tree search always plays out a full tree. It is mostly optimized to keep the tree as small as possible. 
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

template<typename this_t, typename HYP, typename callback_t>
class MCTSBase : public ParallelInferenceInterface<HYP>, public BaseNode<this_t> {
	friend class BaseNode<this_t>;
		
public:

	using data_t = typename HYP::data_t;

	bool open; // am I still an available node?
		
	SpinLock mylock; 
	
	int which_expansion; 
	
	// these are static variables that makes them not have to be stored in each node
	// this means that if we want to change them for multiple MCTS nodes, we need to subclass
	static double explore; 
	static data_t* data;
	static callback_t* callback; 

	unsigned int nvisits;  // how many times have I visited each node?
	float max;
	float min;
	float lse;
	float last_lp;
    
	MCTSBase() {
	}
	
    MCTSBase(HYP& start, this_t* par,  size_t w) : 
		BaseNode<this_t>(0,par,0),
		open(true), which_expansion(w), nvisits(0), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		mylock.lock();
			
		this->reserve_children(start.neighbors());
		mylock.unlock();
    }
    
    MCTSBase(HYP& start, double ex, data_t* d, callback_t& cb) : 
		BaseNode<this_t>(),
		open(true), which_expansion(0), nvisits(0), max(-infinity), min(infinity), lse(-infinity), last_lp(-infinity) {
		// This is the constructor that gets called from main, and it sets the static variables. All the other constructors
		// should use the above one
		
        mylock.lock();
		
		this->reserve_children(start.neighbors());
		explore = ex; 
		data = d;
		callback = &cb;
		
		mylock.unlock();
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	MCTSBase(const this_t& m) { // because what happens to the child pointers?
		throw YouShouldNotBeHereError("*** should not be copying or moving MCTS nodes");
	}
	
	MCTSBase(this_t&& m) {
		// This must be defined for us to emplace_Back, but we don't actually want to move because that messes with 
		// the multithreading. So we'll throw an exception. You can avoid moves by reserving children up above, 
		// which should make it so that we never call this		
		throw YouShouldNotBeHereError("*** This must be defined for children.emplace_back, but should never be called");
	}
		
	void operator=(const MCTSBase& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
	void operator=(MCTSBase&& m) {
		throw YouShouldNotBeHereError("*** This must be defined for but should never be called");
	}
    
	void print(HYP from, std::ostream& o, const int depth, const bool sort) { 
		// here from is not a reference since we want to copy when we recurse 
		
        std::string idnt = std::string(depth, '\t'); // how far should we indent?
        
		std::string opn = (open?" ":"*");
		
		o << idnt TAB opn TAB  max TAB "visits=" << nvisits TAB which_expansion TAB from ENDL;
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array because otherwise its not const			
			std::vector<std::pair<this_t*,int>> c2;
			int w = 0;
			for(auto& c : this->get_children()) {
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
				c.first->print(newfrom, o, depth+1, sort);
			}
		}
		else {		
			int w = 0;
			for(auto& c : this->get_children()) {
				HYP newfrom = from; newfrom.expand_to_neighbor(w);
				c.print(newfrom, o, depth+1, sort);
				++w;;
			}
		}
    }
 
	// wrappers for file io
    void print(HYP& start, const bool sort=true) { 
		print(start, std::cout, 0, sort);		
	}

	void print(HYP& start, const char* filename, const bool sort=true) {
		std::ofstream out(filename);
		print(start, out, 0, sort);
		out.close();		
	}

    void add_sample(const float v) {
		
		if(std::isnan(v)) return;
		
        max = std::max(max,v);	
		
		if(v != -infinity) // keep track of the smallest non-inf value -- we use this instead of inf for sampling
			min = std::min(min, v); 
		
		lse = logplusexp(lse, v);
		
		last_lp = v;
		
		// and propagate up the tree
		if(not this->is_root()) {
			this->parent->add_sample(v);
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
	
	
	/**
	 * @brief If we can evaluate this current node (usually: compute a posterior and add_sample)
	 * @param current
	 */
	virtual void process_evaluable(HYP& current) {
		open = false; // make sure nobody else takes this one
			
		// if its a terminal, compute the posterior
		current.compute_posterior(*data);
		add_sample(current.likelihood);
		(*callback)(current);
	}

	/**
	 * @brief This gets called before descending the tree if we don't have all of our children. 
	 * 		  NOTE: This could add all the children (default) or be overwritten to add one, or none
	 * @param current
	 */
	virtual void add_children(HYP& current) {
		int neigh = current.neighbors(); 
		
		mylock.lock();
		// TODO: This is a bit inefficient because it copies current a bunch of times...
		// this is intentionally a while loop in case another thread has snuck in and added
		while(this->nchildren() < (size_t)neigh) {
			int k = this->nchildren(); // which to expand
			HYP kc = current; kc.expand_to_neighbor(k);
			this->get_children().emplace_back(kc, reinterpret_cast<this_t*>(this), k);
		}		
		mylock.unlock();		
	}


	virtual int sample_child_index(HYP& current) {
		
		assert(this->children.size() > 0) ;
		
		int neigh = current.neighbors(); 
		
		// probability of expanding to each child
		std::vector<double> children_lps(neigh, -infinity);
		
		// first, load up children_lps with whether they have been visited
		bool all_visited = true;
		for(int k=0;k<neigh;k++) { 
			if(this->children[k].open and this->children[k].nvisits == 0) {
				all_visited = false;
				children_lps[k] = current.neighbor_prior(k);
			}
		}
		
		// if all the neighbors have been visited, we'll overwrite everything
		// with UCT
		if(all_visited) {
			for(int k=0;k<neigh;k++) {
				if(this->children[k].open){
					children_lps[k] = (this->child(k).nvisits == 0 ? 0.0 : (this->max / this->child(k).max) +
																		   FleetArgs::explore * sqrt(log(this->nvisits)/this->children[k].nvisits));
				}
			}
		} 
		
		//		for(int k=0;k<neigh;k++) {
		//			CERR k TAB all_visited TAB children_lps[k] TAB current.neighbor_prior(k) TAB current.string() ENDL;
		//		}

		// someitmes we'll get all NaNs, which is bad new sfor sampling
		bool allNaN = true;
		for(int k=0;k<neigh;k++) {
			if(this->children[k].open and not std::isnan(children_lps[k])) {
				allNaN = false;
				break;
			}
		}
		
		if(allNaN) {
			return myrandom(neigh); // just pick at random if all NaN
		}
		else {
			// choose an index into children
			//idx = sample_int_lp(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
			// choose the max (either of prior or of UCT)
			return arg_max_int(neigh, [&](const int i) -> double {return children_lps[i];} ).first;
		}
	}

	/**
	 * @brief This goes down the tree to a node with no children (OR evaluable)
	 * @param current
	 */
	virtual this_t* descend_to_childless(HYP& current) {
		this->nvisits++; // change on the way down so other threads don't follow
			
		if(current.is_evaluable()) {
			return reinterpret_cast<this_t*>(this);
		}
		
		// add missing kids if we need them
		if(this->children.size() < (size_t)current.neighbors()) {
			return reinterpret_cast<this_t*>(this);
		}
		
		// expand 
		auto idx = sample_child_index(current);
		current.expand_to_neighbor(idx); 
		
		return this->children[idx].descend_to_childless(current);
	}
	
	/**
	 * @brief This goes down the tree, sampling children until it finds an evaluable (building a full tree)
	 * @param current
	 */
	virtual this_t* descend_to_evaluable(HYP& current) {
		this->nvisits++; // change on the way down so other threads don't follow
			
		if(current.is_evaluable()) {
			return reinterpret_cast<this_t*>(this);
		}
		
		// add missing kids if we need them
		if(this->children.size() < (size_t)current.neighbors()) {
			this->add_children(current);
		}
		
		// expand 
		auto idx = this->sample_child_index(current);
		current.expand_to_neighbor(idx); 
		
		return this->children[idx].descend_to_evaluable(current);
	}
	
	/**
	 * @brief This is not implemented in MCTSBase because it is different in Partial and Full (below).
	 * 		  So, subclasses must implement this.
	 * @param current
	 */
	virtual void search_one(HYP& current) = 0;
};

// Must be defined for the linker to find them, apparently:
template<typename this_t, typename HYP, typename callback_t>
double MCTSBase<this_t, HYP, callback_t>::explore = 1.0;

template<typename this_t, typename HYP, typename callback_t>
typename HYP::data_t* MCTSBase<this_t, HYP,callback_t>::data = nullptr;

template<typename this_t, typename HYP, typename callback_t>
callback_t* MCTSBase<this_t, HYP,callback_t>::callback = nullptr;



/**
 * @class PartialMCTSNode
 * @author piantado
 * @date 03/07/20
 * @file MCTS.h
 * @brief This is a version of MCTS that plays out children nplayouts times instead of expanding a full tree
 */
template<typename this_t, typename HYP, typename callback_t>
class PartialMCTSNode : public MCTSBase<this_t,HYP,callback_t> {	
	//friend class MCTSBase<this_t,HYP,callback_t>;
	using Super = MCTSBase<this_t,HYP,callback_t>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	
	virtual void search_one(HYP& current) override {
		if(DEBUG_MCTS) DEBUG("PartialMCTSNode SEARCH ONE ", this, "\t["+current.string()+"] ", this->nvisits);
	
		auto c = this->descend_to_childless(current); //sets current and returns the node. 
		
		// if we are a terminal 
		if(current.is_evaluable()) {
			c->process_evaluable(current);
		}
		else {
			// else add the next row of children and choose one to playout
			c->add_children(current); 
			auto idx = c->sample_child_index(current);
			current.expand_to_neighbor(idx); 
			this->playout(current);  // the difference is that here, we call playout instead of search_one
		}
    }
	

	/**
	 * @brief This gets called on a child that is unvisited. Typically it would consist of filling in h some number of times and
	 * 		  saving the stats
	 * @param h
	 */
	virtual void playout(HYP& current) {
		if(current.is_evaluable()) {
			this->process_evaluable(current);
		}
		else {
		
			// keep track of max since that's what we'll store
			double mx = -infinity;
			auto my_cb = [&](HYP& h) {
				if(h.posterior > mx) mx = h.posterior;
				(*this->callback)(h);
			};
			
			PriorInference samp(current.grammar, this->data, my_cb, &current);
			samp.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart));
			this->add_sample(mx);
		}
	}	
};



/**
 * @class FullMCTSNode
 * @author Steven Piantadosi
 * @date 22/12/20
 * @file MCTS.h
 * @brief This is a MCTS node that descends all the way until it builds a tree to a terminal -- usually it uses too much memory
 */
template<typename this_t, typename HYP, typename callback_t>
class FullMCTSNode : public MCTSBase<this_t,HYP,callback_t> {	
	//friend class MCTSBase<this_t,HYP,callback_t>;
	using Super = MCTSBase<this_t,HYP,callback_t>;
	using Super::Super; // get constructors

	using data_t = typename HYP::data_t;
	
	
    virtual void search_one(HYP& current) override {
		
		if(DEBUG_MCTS) DEBUG("MCTS SEARCH ONE ", this, "\t["+current.string()+"] ", this->nvisits);
				
		auto c = this->descend_to_evaluable(current); //sets current and returns the node. 
		
		c->process_evaluable(current);
    } 
	

	/**
	 * @brief This gets called on a child that is unvisited. Typically it would consist of filling in h some number of times and
	 * 		  saving the stats
	 * @param h
	 */
	virtual void playout(HYP& current) {
		if(current.is_evaluable()) {
			this->process_evaluable(current);
		}
		else {
		
			// keep track of max since that's what we'll store
			double mx = -infinity;
			auto my_cb = [&](HYP& h) {
				if(h.posterior > mx) mx = h.posterior;
				(*this->callback)(h);
			};
			
			PriorInference samp(current.grammar, this->data, my_cb, &current);
			samp.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart));
			this->add_sample(mx);
		}
	}	
};