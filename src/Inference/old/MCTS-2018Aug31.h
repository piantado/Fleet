#ifndef MCTS_H
#define MCTS_H

#include <atomic>
#include <string>
#include <iostream>
#include <list>
#include <vector>
#include <mutex>

#include <pthread.h>

#include "StreamingStatistics.h"
#include "Random.h"
#include "BaseNode.h" // for logplusexp and uniform


template<typename t_value> class MCTSNode;
template<typename t_value> void parallel_MCTS(MCTSNode<t_value>* root, unsigned long steps, unsigned long cores);
template<typename t_value> void parallel_MCTS_helper( void* _args);

template<typename t_value>
struct parallel_MCTSargs { 
	MCTSNode<t_value>* root;
	unsigned long steps; 
};

template<typename t_value>
void* parallel_MCTS_helper( void* args) {
	auto a = (parallel_MCTSargs<t_value>*)args;
	a->root->search(a->steps);
	pthread_exit(nullptr);
}

template<typename t_value>
void parallel_MCTS(MCTSNode<t_value>* root, unsigned long steps, unsigned long cores) {
	
	
	if(cores == 1)  { // don't use parallel, for easier debugging
		root->search(steps);
	}
	else {
	
		pthread_t threads[cores]; 
		parallel_MCTSargs<t_value> args[cores];
		
		for(unsigned long t=0;t<cores;t++) {
			
			args[t].steps=steps;
			args[t].root=root;
			
			int rc = pthread_create(&threads[t], nullptr, parallel_MCTS_helper<t_value>, &args[t]);
			if(rc) assert(0);
		}
		
		for(unsigned long t=0;t<cores;t++)  
			pthread_join(threads[t], nullptr);     // wait for all to complete
	}
}


template<typename t_value>
class MCTSNode {
	/* This is an MCTS search tree where we treat the number of "wins" as the number of times we have found a max
	 * better than any others
	 * 
	 * This hopefullyw ill work better than using probability mass
	 * 
	 * */
public:
    std::vector<MCTSNode*> children;

    // stats about myself
    std::atomic<unsigned long>nvisits;  // how many times have I visited each node?
    std::atomic<bool> open; // am I still an available node?
	 
	const bool expand_all_children = false; // when we expand a new leaf, do we expand all children or just sample one (from their priors?) 
	
	double (*compute_playouts)(const t_value*); // a function point to how we compute playouts
	double explore; 
	
    pthread_mutex_t child_modification_lock; // for exploring below
    
	StreamingStatistics statistics;
	
    MCTSNode* parent; // who is my parent?
    t_value* value;
      
    MCTSNode(MCTSNode* par, t_value* v) : parent(par), value(v) {
		// here we don't expand the children because this is the constructor called when enlarging the tree
		explore=par->explore;
		compute_playouts=par->compute_playouts;
		
        initialize();
    }
    
    MCTSNode(double ex, t_value* h0, double cp(const t_value*) ) : compute_playouts(cp), explore(ex), parent(nullptr), value(h0) {
        initialize();        
    }
    
    ~MCTSNode() {
        for(auto c: children)
            delete c;
        delete value;
    }
    
    size_t size() const {
        int n = 1;
        for(auto c : children)
            n += c->size();
        return n;
    }
    
    void initialize() {
		pthread_mutex_init(&child_modification_lock, nullptr); 
        
		nvisits=0;
        open=true;
    }
    
	
    void print(std::ostream& o, const int depth, const bool sort) const {
        std::string idnt = std::string(depth, '\t');
        
        if(parent != nullptr) {
            std::string opn = (open?" ":"*");
			o << idnt << " " << opn << " " << 
				score() << 
				" " << statistics.median() << "\t"  << statistics.max << "\tS=" << nvisits << "\t" << 
				 value->string() << std::endl;
        }
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array
			
			std::vector<MCTSNode*> c2 = children;			
			std::sort(c2.begin(), c2.end(), [](const auto a, const auto b) {return a->score() > b->score();});

			for(auto c : c2) c->print(o, depth+1, sort);
		}
		else {		
			for(auto c : children) c->print(o, depth+1, sort);
		}
    }
 
	// wrappers for file io
    void print(const bool sort=false) const { 
		print(std::cout, 0, sort);		
	}
	void printerr(const bool sort=false) const {
		print(std::cerr, 0);
	}    
	void print(const char* filename, const bool sort=false) const {
		std::ofstream out(filename);
		print(out, 0, sort);
		out.close();		
	}

//	double score() const {
//		// Think of it this way: you get to pick one, and then an adversary gets to pick one of the remaining. 
//		// you each then sample a hypothesis below. What is the probability that you beat them?		
//		// smarter implementation -- this current version only computes the prob I exceed the others median
//		// this has a kind of game-theoretic probability interpretation?
//		// but we should sort and do it for real: https://stats.stackexchange.com/questions/71531/probability-that-a-draw-from-one-sample-is-greater-than-a-draw-from-a-another-sa
//		
//		//std::vector<double> scores;
//		
//		
//		
//		
//		
//		
//		
//		
//		
//		
//		
//		// todo: put everything in a vector and od the right rank-sum test 
//		
//		// aND CHECK THE ORDER BELOW
//		
//		
//		
//		
//		
//		
//		if(this->parent == nullptr || !open) return 0.0;
//		
//		// compute the best other choice that could be made
//		double P = -infinity;
//		for(auto other : parent->children){
//			double v = statistics.p_exceeds_median(other->statistics); // the probability I exceed other's choice
//			if(v > P) P = v; // TODO: SHOOULDN TTHIS BE THE MIN????
//		}
//
//		return P + explore * sqrt(2.0 * log((double)parent->nvisits+1)/(nvisits+1) );
//	}
	
	double score() const {
		// Score a node (for sampling). This may be overwritten in subclasses 
		
		if(this->parent == nullptr || !open) return 0.0;
		
		// compute the probability that I exceed any of the other children
		double pe = 0.0;
		size_t n  = 0;
		for(auto c : parent->children){
			pe += statistics.p_exceeds_median(c->statistics);
		}
		double P = (pe-0.5)/(n-1); // remove the fact that I counted myself
//		return maxp + explore * sqrt(2.0 * log(this->parent.nvisits+1) / (c->nvisits+1) )
//		return double(wins)/nvisits + explore * sqrt(2.0 * log((double)parent->nvisits+1)/(nvisits+1) );
//		return statistics.p_exceeds_median(parent->statistics) + explore * sqrt(2.0 * log((double)parent->nvisits+1)/(nvisits+1) );
		return P + explore * sqrt(2.0 * log((double)parent->nvisits+1)/(nvisits+1) );
	}
        
    int sample_child() {
		
                		
		// first see if there are any immediate children that are unsearched
		// choose the highest prior first
		{
			int i=0;
			int highest_i=0;
			double highest_prior = -infinity;
			for(auto const c: children){
				if(c->open && c->nvisits == 0){
					double p = c->value->compute_prior();
					
					if(p > highest_prior){
						highest_i = i;
						highest_prior = p;
					}
				}
				++i;
			}
			if(highest_prior > -infinity){
				return highest_i; 
			}
			
		}
		
		// otherwise sample according to the score
		int i=0;
		int best_i = -1;
		double best_score = -infinity; // best found so far
        for(auto const c: children) {
			double s = c->score();
			
			if(s >= best_score) {
				best_score = s;
				best_i = i;
			}
			
            ++i;           
        }
		
        return best_i;
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

		pthread_mutex_lock(&child_modification_lock); 
		if(children.size() == 0) { // check again in case someone else has edited in the meantime
			
			size_t N = value->count_neighbors();
			if(N==0) { // remove myself from the tree since these routes have been explored
				open = false;
			} 
			else {
				open = true;
				for(size_t ei = 0; ei<N; ei++) {
					size_t eitmp = ei; // since this is passed by refernece
					
					auto v = value->make_nth_neighbor(eitmp);
										
					MCTSNode<t_value>* c = new MCTSNode<t_value>(this, v);
					
					children.push_back(c);
				}
			}
		}
		pthread_mutex_unlock(&child_modification_lock); 
				
	}
   
	// search for some number of steps
	void search(unsigned long steps) {
		for(unsigned long i=0;i<steps && !stop;i++){
		   this->search_one();
		}
	}
	
   
    void search_one() {
        // sample a path down the tree and when we get to the end
        // use random playouts to see how well we do
        assert(value != nullptr || parent==nullptr); // only the root gets a null value
		
//        if(value != nullptr) { // the root
//            std::cout << "MCTS SEARCH " <<  pthread_self() << "\t" <<  this << "\t[" << value->string() << "] " << nvisits << std::endl;
//        }
		
		if(nvisits == 0) { // I am a leaf of the search who has not been expanded yet
			add_sample(compute_playouts(value), 1); // update my internal counts
			open = value->is_partial(); // only open if the value is partial
			return;
		}
		else if(children.size() == 0) { // if I have no children
			this->add_child_nodes(); // add child nodes if they don't exist
				
			if(expand_all_children){
				// Include this if when we add child nodes, we need to go through and
				// compute playouts on all of them. If we don't do this, we'd only choose one
				// and then the one we choose the first time determines the probability of coming
				// back to all the others, which may mislead us (e.g. we may never return to a good playout)
				for(auto c: children) {
					nvisits++; // I am going to get a visit for each of these
					
					c->add_sample(c->compute_playouts(value), 1); // update my internal counts
					c->open = c->value->is_partial(); // only open if the value is partial
					
					open = open || c->open; // I'm open if any child is
				}
				return; // this counts as a search move
			}
		}
		
		// choose an existing child and expand (even if we just added them above!)
			
		// Now choose a child from their weights, but note that our weights will
		// preferentially choose nodes with nvisits=0 since these are the unexpanded leaves
		int which_child = this->sample_child(); 
		
		nvisits++; // increment our visit count on our way down so other threads don't follow
		
		if(which_child==-1) { // this is a sign this node is done
			open = false;
		}
		else {
			children[which_child]->search_one(); // and recurse         
		}
		
    } // end search


};


#endif