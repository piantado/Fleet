
#pragma once 

//#define DEBUG_MCTS 1

#include <atomic>
#include <mutex>
#include <set>
#include <algorithm>
#include <iostream>
#include <fstream>

#include "StreamingStatistics.h"




/// MCTS Implementation
template<typename HYP>
class MCTSNode {
	// NOTE: When we initialize a MCTSNode, we typically will want it with a LOTHypothesis with a null value		
	
public:

	// use a strict enum of how children are scored in sampling them
	// UCBMAX -- a method inspired by UCB sampling, but using the max of all hypotheses found by that route
	// SAMPLE -- sample from (a subsample) of the posteriors we find below a node
	// MEDIAN -- choose if my kid tends to beat the median of the parent
	enum class ScoringType { SAMPLE, UCBMAX, MEDIAN};	

    std::vector<MCTSNode> children;

	// NOTE: These used to be atomics?
    unsigned long nvisits;  // how many times have I visited each node?
    bool open; // am I still an available node?
	 
	bool expand_all_children = false; // when we expand a new leaf, do we expand all children or just sample one (from their priors?) 
	
	double (*compute_playouts)(const HYP&); // a function point to how we compute playouts
	double explore; 
	
	mutable std::mutex child_mutex; // for access in parallelTempering
    
	StreamingStatistics statistics;
	
	
    MCTSNode* parent; // who is my parent?
    HYP value;
    ScoringType scoring_type;// how do I score playouts?
  
    MCTSNode(MCTSNode* par, HYP& v) 
		: expand_all_children(par->expand_all_children), compute_playouts(par->compute_playouts), 
		  explore(par->explore), parent(par), value(v), scoring_type(par->scoring_type) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		
        initialize();	
    }
    
    MCTSNode(double ex, HYP& h0, double cp(const HYP&), ScoringType st=ScoringType::SAMPLE ) : 
		compute_playouts(cp), explore(ex), parent(nullptr), value(h0), scoring_type(st) {
        initialize();        
    }
	
	// should not copy or move because then the parent pointers get all messed up 
	MCTSNode(const MCTSNode& m) = delete;
	MCTSNode(MCTSNode&& m) {
		std::lock_guard guard(m.child_mutex); // get m's lock before moving
		std::lock_guard guard2(child_mutex); // and mine to be sure
		
		assert(m.children.size() ==0); // else parent pointers get messed up
		nvisits = m.nvisits;
		open = m.open;
		expand_all_children = m.expand_all_children;
		compute_playouts = m.compute_playouts;
		explore = m.explore;
		statistics = std::move(statistics);
		parent = m.parent;
		value = std::move(m.value);
		scoring_type = m.scoring_type;
		children = std::move(children);
		
	}
	
	
    size_t size() const {
        int n = 1;
        for(auto c : children)
            n += c->size();
        return n;
    }
    
    void initialize() {
		nvisits=0;
        open=true;
    }
    
	
    void print(std::ostream& o, const int depth, const bool sort) const { 
        std::string idnt = std::string(depth, '\t');
        
		std::string opn = (open?" ":"*");
		o << idnt TAB opn TAB score() TAB statistics.median() TAB statistics.max TAB "S=" << nvisits TAB value.string() ENDL;
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array because otherwise its not const			
			std::vector<const MCTSNode*> c2;
			for(const auto& c : children) c2.push_back(&c);
			std::sort(c2.begin(), c2.end(), [](const auto a, const auto b) {return a->statistics.N > b->statistics.N;}); // sort by how many samples

			for(const auto& c : c2) c->print(o, depth+1, sort);
		}
		else {		
			for(auto& c : children) c.print(o, depth+1, sort);
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

	double score() const {
		// Compute the score of this node, which is used in sampling down the tree
		// NOTE: this can't be const because the StreamingStatistics need a mutex
		
		if(scoring_type == ScoringType::SAMPLE) {
			// I will add probability "explore" pseudocounts to my parent's distribution in order to form a prior 
			// please change this parameter name later. 
			if(uniform() <= explore / (explore + statistics.N)) {
				
				// here, if we are the base root node, we treat all "parent" samples as 0.0
				return (this->parent == nullptr ? statistics.sample() : parent->score()); // handle the base case			
			} 
			else {
				return statistics.sample();			
			}		
		}
		else if(scoring_type == ScoringType::UCBMAX) {
			// score based on a UCB-inspired score, using the max found so far below this
			if(statistics.N == 0 || parent == nullptr) return infinity; // preference for unexplored nodes at this level, thus otherwise my parent stats must be >0
			return statistics.max + explore * sqrt(log(parent->statistics.N+1) / log(1+statistics.N));
			
		}
		else if(scoring_type == ScoringType::MEDIAN) {
			if(statistics.N == 0 || parent == nullptr) return infinity; // preference for unexplored nodes at this level, thus otherwise my parent stats must be >0
			return statistics.p_exceeds_median(parent->statistics) + explore * sqrt(log(parent->statistics.N+1) / log(1+statistics.N));
		}
		else {
			assert(false && "Invalide ScoringType specified in MCTS::score");
		}
	}

	size_t open_children() const {
		size_t n = 0;
		for(auto const& c : children) {
			n += c.open;
		}
		return n;
	}


	size_t best_child_index() const {
		// returns a pointer to the best child according to the scoring function
		// NOTE: This might return a nullptr in highly parallel situations
		std::lock_guard guard(child_mutex);
		
		size_t best_index = 0;
		double best_score = -infinity;
		size_t idx = 0;
		for(auto& c : children) {
			if(c.open) {
				//if(c->statistics.N==0) return c; // just take any unopened
				double s = c.score();
				if(s >= best_score) {
					best_score = s;
					best_index = idx;
				}
			}
			idx++;
		}
		return best_index;
	}


    void add_sample(const double v, const size_t num){ // found something with v
        
		nvisits += num; // we want to update this even if inf/nan since we did try searching here
		
		statistics << v;
		
		// and add this sampel going up too
		if(parent != nullptr) {
			parent->add_sample(v, 0); // we don't add num going up because we incremented it on the way down (while sampling)
		}
    }
	
	
	void add_child_nodes() {

		std::lock_guard guard(child_mutex);
		
		if(children.size() == 0) { // check again in case someone else has edited in the meantime
			
			size_t N = value.neighbors();
			//std::cerr << N TAB value->string() ENDL;
						
			if(N==0) { // remove myself from the tree since these routes have been explored
				open = false;
			} 
			else {
				open = true;
				for(size_t ei = 0; ei<N; ei++) {
					size_t eitmp = ei; // since this is passed by refernece
					
					auto v = value.make_neighbor(eitmp);
					
#ifdef DEBUG_MCTS
        if(value != nullptr) { // the root
            std::cout TAB "Adding child " <<  this << "\t[" << v->string() << "] " ENDL;
        }
#endif
					//children.push_back(MCTSNode<HYP>(this,v));
					children.emplace_back(this,v);
				}
			}
		}
	}
   
	// search for some number of steps
	void search(unsigned long steps, unsigned long time) {
		
		using clock = std::chrono::high_resolution_clock;
		
		auto start_time = clock::now();
		for(unsigned long i=0; (i<steps || steps==0) && !CTRL_C;i++){
			
#ifdef DEBUG_MCTS
			if(value != nullptr) { // the root
				COUT "MCTS SEARCH LOOP" TAB i TAB std::this_thread::get_id() ENDL;
			}
#endif
			if(time > 0) {
				double elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start_time).count();
				if(elapsed_time > time) break;
			}
			
		   this->search_one();
		}
	}
	static void __helper(MCTSNode<HYP>* h, unsigned long steps, unsigned long time) {
		h->search(steps, time);
	};
	
	void parallel_search(unsigned long cores, unsigned long steps, unsigned long time) { 
		
		std::thread threads[cores]; 
			
		// start everyone running 
		for(unsigned long i=0;i<cores;i++) {
			threads[i] = std::thread(__helper, this, steps, time);
		}
		
		for(unsigned long i=0;i<cores;i++) {
			threads[i].join();
		}	
	}

	
	
   
    void search_one() {
        // sample a path down the tree and when we get to the end
        // use random playouts to see how well we do
        assert(parent==nullptr); // only the root gets a null value

	//	MYDEBUG(DEBUG_MCTS, "MCTS Search", this, value->string(), nvisits);

//#ifdef DEBUG_MCTS
//        if(value != nullptr) { // the root
//            COUT "MCTS SEARCH " <<  this << "\t[" << value->string() << "] " << nvisits TAB std::this_thread::get_id() ENDL;
//        }
//#endif
		
		if(nvisits == 0) { // I am a leaf of the search who has not been expanded yet
			add_sample(compute_playouts(value), 1); // update my internal counts
			open = value.neighbors() > 0; // only open if the value is partial
			return;
		}
		
		
		if(children.size() == 0) { // if I have no children
			this->add_child_nodes(); // add child nodes if they don't exist
				
			if(expand_all_children){
				// Include this if when we add child nodes, we need to go through and
				// compute playouts on all of them. If we don't do this, we'd only choose one
				// and then the one we choose the first time determines the probability of coming
				// back to all the others, which may mislead us (e.g. we may never return to a good playout)
				for(auto& c: children) {
					nvisits++; // I am going to get a visit for each of these
					
					c.add_sample(c.compute_playouts(value), 1); // update my internal counts
					c.open = c.value.neighbors() > 0; // only open if the value is partial
					
					open = open || c.open; // I'm open if any child is
				}
			}
			
			
			
		}
		
		// even after we try to add, we might not have any. If so, return
		if(open_children() == 0) {
			open = false;
			return;
		}
		
		// now if we get here, we must have at least one open child
		// so we can pick the best and recurse 
		
		nvisits++; // increment our visit count on our way down so other threads don't follow
		
		// choose the best and recurse
		size_t bi = best_child_index();
		children[bi].search_one();
		
    } // end search


};

//template<typename HYP>
//class FullMCTSNode : public MCTSNode<HYP> {
//	// In this version, when we go down to build a tree, we include ALL of the nodes
//	
//	void add_child_nodes()=delete; // we don't use this 
//	
//}