#ifndef MCTS_H
#define MCTS_H

#include <string>
#include <iostream>
#include <list>
#include <vector>

#include <pthread.h>

#include "Random.h"
#include "BaseNode.h" // for logplusexp and uniform

//class MCTSable { 
//    virtual size_t    count_neighbors() = 0;
//    virtual MCTSable* get_nth_neighbor(int n) = 0;
//};


template<typename t_value>
class MCTSNode {
public:
    std::vector<MCTSNode*> children;

    // stats about myself
    unsigned long nsamples;  // how many samples have run through me?
    double maxp; // what is the max posterior I've found?
    double sumlogp; // what is the sum of my log ps?
    bool open; // am I still an available node?
    
    pthread_mutex_t search_lock; // for exploring below
    pthread_mutex_t stats_lock;  // for updating stats
    
    MCTSNode* parent; // who is my parent?
    t_value* value;
      
    MCTSNode(MCTSNode* par, t_value* v) {
        pthread_mutex_init(&search_lock, nullptr); // initialize the lock        
        pthread_mutex_init(&stats_lock, nullptr); // initialize the lock        
        
        pthread_mutex_lock(&search_lock);
        initialize(par,v);
        pthread_mutex_unlock(&search_lock);
        
    }
    
    MCTSNode(auto init, void compute_playouts(const t_value*, MCTSNode<t_value>*) ) {
        pthread_mutex_init(&search_lock, nullptr); // initialize the lock        
        pthread_mutex_init(&stats_lock, nullptr); // initialize the lock        
        
        pthread_mutex_lock(&search_lock);
        initialize(nullptr,nullptr);
        
        for(auto x : init) {
            MCTSNode* c = new MCTSNode(this, x);
            compute_playouts(x,c);
//            cerr << "Initialize MCTS " << x->string() << "\t" << x->is_partial() << endl;
            
            if(x->is_partial()) {
                children.push_back(c); // add this to the tree if partial
            }
            else {
                delete c; // don't need anything else 
            };
        }

        pthread_mutex_unlock(&search_lock);
        
        recompute_stats();
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
    
    void initialize(MCTSNode* par, t_value* v) {
        parent = par;
        value = v;
        
        nsamples=0;
        maxp=-infinity;
        sumlogp=0.0;
        open=true;
    }
    
    void print() { print(0); }
    
    void print(int depth) {
        using namespace std;
        
        char buf[STR_BUF_SIZE];
        string idnt = string(depth, '\t');
        
        if(parent != nullptr) {
            
            // compute my score
            double z = -infinity;
            for(auto const c: parent->children) {
                if(c->open) z = logplusexp(z, c->maxp);
            }
            double score = exp( maxp - z) + sqrt(log(nsamples) / parent->nsamples); /// NOTE: This shows score on explr=1
            
            string opn = (open?" ":"*");
            printf("#%s %s %.2f %lu %.2f", idnt.c_str(), opn.c_str(), maxp, nsamples, score);
            value->str(buf);
            printf("\t%s\n", buf);
        }
        
        for(auto c : children)
            c->print(depth+1);
    }
 
        
    int sample(const double explr) {
        double best_score = -infinity; // best found so far
        int best_i = -1;
        
        assert(children.size()>0);
                
        double z = -infinity;
        for(auto const c: children) {
           // if(c->open) z = logplusexp(z, c->maxp);
            if(c->open) z = logplusexp(z, c->sumlogp/(double)nsamples);
        }
        
        int i=0;
        for(auto const c: children) {
            if(c->open){
                //double score = exp( c->maxp - z) + explr * sqrt(2.0 * log(c->nsamples) / nsamples);
               double score = exp( c->sumlogp/(double)nsamples - z) + explr * sqrt(2.0 * log(nsamples) / c->nsamples);
               
                // add a little nosie so that  different threads choose differnet choices typically
                score = score + 0.001*uniform(rng);
                
                if (score > best_score) {
                    best_score = score;
                    best_i = i;
                }
            }
            i++;            
        }
        return best_i;
    }
    
    void recompute_stats() {
        // Go up the tree recomputing all the stats
        // we do this ignoring non-open nodes, since those can't be exploreds

        if(children.size() > 0){
            // we never recompute on the leaves, otherwise zeroing 
            // kills the leaf stats and then that propagates up the tree

            pthread_mutex_lock(&stats_lock);
            maxp = -infinity;
            nsamples = 0;
            open = false;
            sumlogp = 0.0;
            for(auto c: children) {
                if(c->open) {
                    maxp = std::max<double>(c->maxp, maxp);
                    open = true; // if any are open (otherwise they are ignored here))
                    sumlogp += c->sumlogp;
                }   
                nsamples += c->nsamples; // these get used even for closed nodes
            }
            pthread_mutex_unlock(&stats_lock);
        } //else { open=false; }

        // and recurse up the tree
        if(parent != nullptr)
            parent->recompute_stats();
        
    }
    

    void add_sample(double v, size_t num){ // found something with v
        pthread_mutex_lock(&stats_lock);
        
        if(!std::isnan(v)){ // don't include nans/infs in scores
            maxp = std::max<double>(v,maxp);
            sumlogp += v; 
        }
        nsamples += num; // but count as samples
        
        pthread_mutex_unlock(&stats_lock);
    }
	
   
    
    
};


#endif