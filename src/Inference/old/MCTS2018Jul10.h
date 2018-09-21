#ifndef MCTS_H
#define MCTS_H

#include <atomic>
#include <string>
#include <iostream>
#include <list>
#include <vector>
#include <mutex>

#include <pthread.h>

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
    std::atomic<unsigned long>nsamples;  // how many samples have run through me?
    std::atomic<double> totalp; // what is the total posterior mass that I
	std::atomic<double> maxp; // the max I've found
	std::atomic<unsigned long> nmax; // how many times have I yielded the max below here?
    std::atomic<double> sumlogp; // what is the sum of my log ps?
    std::atomic<bool> open; // am I still an available node?
	std::atomic<size_t> numunsearched; // how many are unsearched?
    
	// search parameters -- stored in each node
	double (*compute_playouts)(const t_value*); // a function point to how we compute playouts
	double explore; 
	
    pthread_mutex_t search_lock; // for exploring below
	pthread_mutex_t child_modification_lock; // for exploring below
    pthread_mutex_t stats_lock;  // for updating stats
    
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
		pthread_mutex_init(&search_lock, nullptr); // initialize the lock   
		pthread_mutex_init(&child_modification_lock, nullptr); 
        pthread_mutex_init(&stats_lock, nullptr); 
        
		pthread_mutex_lock(&stats_lock);
        maxp=-infinity; // Start by initializing this to infinity so any node with infs in it will be preferentially chosen
		nmax=0;
        nsamples=0;
        totalp=-infinity;
        sumlogp=0.0;
		numunsearched = 0;
        open=true;
		pthread_mutex_unlock(&stats_lock);        
    }
    
	
    void print(std::ostream& o, const int depth, const bool sort) const {
        std::string idnt = std::string(depth, '\t');
        
        if(parent != nullptr) {
            std::string opn = (open?" ":"*");
			o << idnt << " " << opn << " " << maxp << "\tn=" << nsamples << " (" << numunsearched << ")\t" <<
				 value->count_neighbors() << "\t" << value->string() << std::endl;
        }
		
		// optional sort
		if(sort) {
			// we have to make a copy of our pointer array
			
			std::vector<MCTSNode*> c2 = children;
			
			std::sort(c2.begin(), c2.end(), [](const auto a, const auto b) {return a->maxp > b->maxp;});

			for(auto c : c2)
				c->print(o, depth+1, sort);

		}
		else {
		
			for(auto c : children)
				c->print(o, depth+1, sort);
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
        
    int sample_child() {
		double best_score = -infinity; // best found so far
		size_t best_n = SIZE_MAX;
        int best_i = -1;
        
		// check if there are any kids 
		// (no guarantee there are some due to multithreading)
		size_t nopen = 0;
		for(auto const c: children) {
			if(c->open) nopen++;
		}
		if(nopen == 0) return -1; 
		
        		
		// first see if there are any immediate children that are unsearched
		int i=0;
		for(auto const c: children){
			if(c->open && c->nsamples == 0){
				return i;
			}
			++i;
		}
		
		// now check if there are any later children that are unsearched
		if(numunsearched > 0) { 
			size_t mx = 0;
			for(auto const c: children){
				if(c->open && c->numunsearched > mx) {
					mx = c->numunsearched;
				}
			}
			//assert(mx > 0);-- can't assert since numunsearched migth have changed

			i=0;
			for(auto const c: children){
				if(c->open && c->numunsearched == mx){
					return i;
				}
				++i;
			}
		}		

		// otherwise sample according to the score
		i=0;
        for(auto const c: children) {
            if(c->open){ 
				// compute the explore part of score
				double score = explore * sqrt(2.0 * log(this->nsamples+1) / (c->nsamples+1) );
				
				// need to prevent nan scores when everything is -inf or otherwise we always sample it
				// so this effectively treats this->maxp as zero influence on score
				if(this->maxp > -infinity) {
					score += exp( c->maxp - this->maxp);
				}
				
				// pick the best score or the fewest samples (if best scores are equal)
				// this latter condition is important for parallel, where we might have multiple
				// children have inf score, and they get sampled when another process starts on them
				if(score >= best_score || (score==best_score && c->nsamples < best_n) ) {
					best_score = score;
					best_i = i;
					best_n = c->nsamples;
				}
				
            }
            ++i;           
        }
		
        return best_i;
    }
    
    void recompute_stats() {
        // Go up the tree recomputing all the stats
        // we do this ignoring non-open nodes, since those can't be exploreds
		
		//cerr << "Recomputing stats on " << (value!=nullptr?value->string():"") << endl;
		
        if(children.size() > 0){
            // we never recompute on the leaves, otherwise zeroing 
            // kills the leaf stats and then that propagates up the tree

            pthread_mutex_lock(&stats_lock);
            totalp = -infinity;
            nsamples = 0;
			open = false;
            sumlogp = 0.0;
			maxp=-infinity;
			nmax=0;
			numunsearched = 0;
            for(auto c: children) {
				totalp = logplusexp(c->totalp, totalp); // NOTE: Should I do this only over open children?
				sumlogp = sumlogp + c->sumlogp;
				
				if(c->nsamples == 0) {
					numunsearched++; // I see one unsearched below me
				}
				else {
					numunsearched += c->numunsearched;
				}
				
				if(c->open) { // I am open if any of my children are
					open = true; 
				}
 
				if(c->maxp > maxp) {  // should I use only open nodes? 
					maxp = (double)c->maxp;
					c->nmax++; // increment this child's counter -- NOTE this only makes sense as we recurse UP the tree
				}
 
				nsamples += c->nsamples; // these get used even for closed nodes
            }
            pthread_mutex_unlock(&stats_lock);
        } //else { open=false; }
		//cerr << "\t" << totalp <<"\t" << this->totalp << endl;
		
        // and recurse up the tree
        if(parent != nullptr)
            parent->recompute_stats();
        
    }
    

    void add_sample(const double v, const size_t num){ // found something with v
        pthread_mutex_lock(&stats_lock);
        if(!std::isnan(v)){ // don't include nans/infs in scores
            totalp = logplusexp(totalp, v);
			sumlogp = sumlogp + v; 
			if(v > maxp) maxp = v; // store this
        }
		
        nsamples += (num-1); // we got one sample just by visiting this node so only add anything else
		
        pthread_mutex_unlock(&stats_lock);
    }
	
	
	void add_child_nodes() {
		size_t N = value->count_neighbors();
		if(N==0) { // remove myself from the tree since these routes have been explored
			open = false;
		} 
		else {
			for(size_t ei = 0; ei<N; ei++) {
				size_t eitmp = ei; // since this is passed by refernece
				
				auto v = value->make_nth_neighbor(eitmp);
									
				MCTSNode<t_value>* c = new MCTSNode<t_value>(this, v);
				
				children.push_back(c);
			}
		}
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
//            std::cout << "MCTS SEARCH " <<  pthread_self() << "\t" <<  this << "\t[" << value->string() << "]" << std::endl;
//        }
		
		if(nsamples == 0) { // I am a leaf of the search who has not been expanded yet
			pthread_mutex_lock(&child_modification_lock); // need this because otherwise we might update nsamples when a child is being selected
			++nsamples;
			double score = compute_playouts(value); // compute the playouts with c as the node
			add_sample(score,1); // update my internal counts
			open = value->is_partial(); // only open if the value is partial
			recompute_stats(); // go back up the tree updating the stats
			pthread_mutex_unlock(&child_modification_lock); 
			
		}
		else {
			
			pthread_mutex_lock(&child_modification_lock); 
			if(children.size() == 0) { 
				this->add_child_nodes(); // add child nodes if they don't exist
			}
			
		    // Now choose a child from their weights, but note that our weights will
			// preferentially choose nodes with nsamples=0 since these are the unexpanded leaves
            int which_child = this->sample_child(); 
			
			++nsamples; // increment our sample count on our way down, with the lock, so other threads don't follow
			
			if(which_child==-1) { // this is a sign this node is done
                open = false;
                recompute_stats();
            }
			
			pthread_mutex_unlock(&child_modification_lock); // maintain the lock until we are done
			
			
			if(which_child >= 0) {
				
                children[which_child]->search_one(); // and recurse         

			}
			
        }
		
    } // end search


//	void searchAndExpand_DEFUNCT(const double explr, double callback(t_value* h) ) {
//		// Like a standard MCTS move, but we sample children and build the tree as we go down.
//		// Defaultly we sample children by their prior
//		// this one's callback will return teh posterior score of the node h, which is only called on terminals
//		// The main difficult here is that this is very memory intensive 
//		
//        assert(value != nullptr || parent==nullptr); // only the root gets a null value
//		//if(value != nullptr) cerr << "MCTS " << value->string() << endl;
//
//        if(children.size() > 0){ // going down the tree
//            //printf("A");
//            int i = this->sample_child(explr); // sample from the weights
//
//            if(i==-1) { // this is a sign this node is done
//                open = false;
//                recompute_stats();
//            }
//            else { // otherwise continue our search down   
//                children[i]->searchAndExpand(explr, callback); // and recurse           
//            }
//        }
//        else { // reach a root node
//            pthread_mutex_lock(&search_lock); 
//			
//            // leaf node so random playout
//            size_t N = value->count_neighbors();
//
//            if(N==0) { // I am a terminal
//			
//				double val = callback(value);
//				add_sample(val, 1);
//				open=false;
//				this->recompute_stats();
//            } 
//			else { 
//				// TODO: The problem is that we generate everything here... we should be able to get away with only one node...
//				
//				// otherwise fan open this branch
//                for(size_t ei = 0; ei<N; ei++) {
//                    size_t eitmp = ei; // since this is passed by refernece
//                    
//                    auto v = value->make_nth_neighbor(eitmp);
//					
//					MCTSNode<t_value>* c = new MCTSNode<t_value>(this, v);
//					
//					c->totalp = v->compute_prior(); // set this initially to the prior
//					
//                    children.push_back(c); // add this to the 
//					
////					if(v->count_neighbors() == 0) {
////						double val=callback(v);
////						add_sample(val,1);
////						c->open = false;
////					}
//                }
//				 
//				// now sample from the children according to their prior
//				size_t ci = sample_via<MCTSNode*>(children, [](MCTSNode* x){ return x->value->compute_prior(); });
//				//size_t ci = sample_via<MCTSNode*>(children, [](MCTSNode* x){ return x->totalp; });
//				
//				// and recurse
//				children[ci]->searchAndExpand(explr, callback); // and recurse           
//				recompute_stats();
//				
//            }
//            
//            pthread_mutex_unlock(&search_lock); 
//            
//            // and finally at the end, recompute all the stats
//            recompute_stats();
//        
//        }   
//        
//        
//    }  
    
};


#endif