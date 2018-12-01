#ifndef HYPOTHESIS_H
#define HYPOTHESIS_H

#include<string.h>
#include "Proposers.h"



template<typename HYP, typename T, t_nonterminal nt, typename t_input, typename t_output>
class LOTHypothesis : public Dispatchable<t_input,t_output>, 
				      public MCMCable<HYP,t_input,t_output>, // remember, this defines t_data, t_datum
					  public Searchable<HYP,t_input,t_output>
{
	// stores values as a pointer to something of type T, whose memory I manage (I delete it when I go away)
	// This also stores a pointer to a grammar, but I do not manage its memory
	// nt store the value of the root nonterminal
	// HYP stores my own type (for subclasses) so I know how to cast copy and propose
public:     
	typedef typename MCMCable<HYP,t_input,t_output>::t_data t_data;
	typedef typename MCMCable<HYP,t_input,t_output>::t_datum t_datum;
	
	Grammar* grammar;
	T* value;

	LOTHypothesis(Grammar* g, T* x) : grammar(g) {
		set_value(x);
	}

	// create via sampling
	LOTHypothesis(Grammar* g) : grammar(g) {
		set_value(grammar->generate<T>(nt));
	}

	// copy constructor
	LOTHypothesis(const LOTHypothesis& h) : MCMCable<HYP,t_input,t_output>(h),
		grammar(h.grammar) { // prior, likelihood, etc. should get copied 
		set_value(h.value==nullptr?nullptr:h.value->copy());
	}
	
	LOTHypothesis(LOTHypothesis&& h)=delete;
	
	virtual ~LOTHypothesis() {
		delete value;
		// don't delete grammar -- we don't own that. 
	}
	
	virtual void set_value(T* x) {
		value = x;
	}
	
	
	HYP* copy() const {
		// create a copy
		HYP* x = new HYP(grammar, nullptr);
		if(value != nullptr) 
			x->set_value(value->copy());
		x->prior      = this->prior;
		x->likelihood = this->likelihood;
		x->posterior  = this->posterior;
		x->fb         = this->fb;
		return x; 
	}
	
	// Stuff to create hypotheses:
	virtual HYP* propose() const {
		// tODO: Check how I do fb here?
		
		HYP* ret = new HYP(grammar, nullptr);
		
		if(value == nullptr) {
			ret->value = grammar->generate<Node>(nt);
			ret->fb = 0.0; // TODO: hmm not sure what to do here
			return ret;
		}
		
		// choose a type:
		if(uniform(rng) < 0.1) { // p of regeneration
			std::tie(ret->value, ret->fb) = regeneration_proposal(grammar, value);
		}
		else {
			if(uniform(rng) < 0.5) {
				std::tie(ret->value, ret->fb) = insert_proposal(grammar, value);
			}
			else {
				std::tie(ret->value, ret->fb) = delete_proposal(grammar, value);
			}
		}
		
		return ret;
	}
	
	virtual HYP* restart() const {
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. So her

		if(value == nullptr || value->can_resample) {
			// just sample from the prior if the root is like this
			return new HYP(grammar);
		}
		else {
			// recurse through the tree and replace anything that we can propose to
			Node* n = value->copy_resample(*grammar, [](const Node* n) { return n->can_resample; }); 
			return new HYP(grammar, n);
		}
		
	}
	
	virtual double compute_prior() {
		this->prior = grammar->log_probability(value);
		return this->prior;
	}
	
	virtual double compute_single_likelihood(const t_datum& datum) {
		// compute the likelihood of a *single* data point. 
		assert(0);// for base classes to implement, but don't set = 0 since then we can't create Hypothesis classes. 
	}

	virtual void push_program(Opstack& s, short k=0) {
		assert(k==0); // this is only for lexica
		assert(value != nullptr);
		
		value->linearize(s);
	}


	// we defaultly map outputs to log probabilities
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err, Dispatchable<t_input,t_output>* loader, double minlp=-10.0){
		assert(value != nullptr);
		
		VirtualMachinePool<t_input,t_output> pool(minlp);

		VirtualMachineState<t_input,t_output>* vms = new VirtualMachineState<t_input,t_output>(x, err);
		
		push_program(vms->opstack); // write my program into vms (loader is used for everything else)
		
		pool.push(vms); // add vms to the pool
		
		return pool.run(this, loader);		
	}
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err) {
		return call(x,err, this); // defaultly I myself am the recursion handler and dispatch
	}
	auto operator()(const t_input x, const t_output err){ // just fancy syntax for call
		return call(x,err);
	}
	
	virtual t_output callOne(const t_input x, const t_output err) {
		// wrapper for when we have only one output
		auto v = call(x,err);
		
		if(v.size() == 0)  // if we get nothing, silently treat that as "err"
			return err;
			
		if(v.size() > 1) { // complain if you got too much output -- this should not happen
			CERR "Error in callOne  -- multiple outputs received" ENDL;
			for(auto x: v.values()) {
				CERR "***" TAB x.first TAB x.second ENDL;
			}
			assert(false); // should not get this		
		}
		for(auto a : v.values()){
			return a.first;
		}
		assert(0);
	}
	
	virtual std::string string() const {
		return std::string("\u03BBx.") + (value==nullptr ? Node::nulldisplay : value->string());
//		return std::string("Lx.") + (value==nullptr ? Node::nulldisplay : value->string());
	}
	virtual size_t hash() const {
		return value->hash();
	}
	
	virtual bool operator==(const LOTHypothesis<HYP,T,nt,t_input,t_output>& h) const {
		return this->value->operator==(*h.value);
	}
	
	virtual t_abort dispatch_rule(op_t op, VirtualMachinePool<t_input,t_output>* pool, VirtualMachineState<t_input,t_output>* vms,  Dispatchable<t_input, t_output>* loader )=0;
	
	virtual HYP* copy_and_complete() const {
		// make a copy and fill in the missing nodes.
		// NOTE: here we set all of the above nodes to NOT resample
		// TODO: That part should go somewhere else eventually I think?
		HYP* h = copy();
		h->prior=0.0;h->likelihood=0.0;h->posterior=0.0; // reset these just in case
		if(h->value == nullptr) {
			h->set_value(grammar->generate<Node>(nt));
		}
		else {
			h->value->map( [](Node* n){n->can_resample=false;} );
			auto t = h->value;
			t->complete(*grammar);
			h->set_value(t); // must call set once its changed
		}
		return h;
	}

	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const {
		if(value == nullptr) { // if the value is null, our neighbors is the number of ways we can do nt
			return grammar->count_expansions(nt);
		}
		else {
			return value->neighbors(*grammar);
		}
	}
	
	virtual HYP* make_neighbor(int k) const {
		HYP* h = new HYP(grammar, nullptr); // new hypothesis
		if(value == nullptr) {
			assert(k >= 0);
			assert(k < (int)grammar->count_expansions(nt));
			auto r = grammar->get_expansion(nt,k);
			h->set_value(grammar->make<Node>(r));
		}
		else {
			auto t = value->copy();
			t->expand_to_neighbor(*grammar,k);
			h->set_value(t);
		}
		return h;
	}
	virtual bool is_evaluable() const {
		// This checks whether it should be allowed to call "call" on this hypothesis. 
		// Usually this means that that the value is complete, meaning no partial subtrees
		return value != nullptr && value->is_evaluable();
	}

	 
};



#endif
