#pragma once


#include <string.h>
#include "Proposers.h"

template<typename HYP, typename T, 
		 typename t_input, typename t_output, 
		 typename _t_datum=default_datum<t_input, t_output>, 
		 typename _t_data=std::vector<_t_datum> >
class LOTHypothesis : public Dispatchable<t_input,t_output>, 
				      public MCMCable<HYP,_t_datum,_t_data>, // remember, this defines t_data, t_datum
					  public Searchable<HYP,t_input,t_output>	{
	// stores values as a pointer to something of type T, whose memory I manage (I delete it when I go away)
	// This also stores a pointer to a grammar, but I do not manage its memory
	// nt store the value of the root nonterminal
	// HYP stores my own type (for subclasses) so I know how to cast copy and propose
public:     
	typedef typename Bayesable<_t_datum,_t_data>::t_data   t_data;
	typedef typename Bayesable<_t_datum,_t_data>::t_datum t_datum;
	
	static const size_t MAX_NODES = 64; // 32 -- does not work for FancyEnglish!; // max number of nodes we allow; otherwise -inf prior
	
	Grammar* grammar;
	T value;

	LOTHypothesis(Grammar* g=nullptr)  : MCMCable<HYP,t_datum,t_data>(), grammar(g), value(NullRule,0.0,true) {}
	LOTHypothesis(Grammar* g, T&& x)   : MCMCable<HYP,t_datum,t_data>(), grammar(g), value(x) {}
	LOTHypothesis(Grammar* g, T& x)    : MCMCable<HYP,t_datum,t_data>(), grammar(g), value(x) {}

	// parse this from a string
	LOTHypothesis(Grammar* g, std::string s) : MCMCable<HYP,t_datum,t_data>(), grammar(g)  {
		value = grammar->expand_from_names(s);
	}
	
	// Stuff to create hypotheses:
	[[nodiscard]] virtual std::pair<HYP,double> propose() const override {
		/**
		 * @brief Default proposal is rational-rules style regeneration. 
		 * @return 
		 */
	
		// simplest way of doing proposals
		auto x = Proposals::regenerate(grammar, value);	
		return std::make_pair(HYP(this->grammar, std::move(x.first)), x.second); // return HYP and fb
	}	

	
	[[nodiscard]] virtual HYP restart() const override {
		/**
		 * @brief This is used to restart chains, sampling from prior
		 * @return 
		 */
		
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. So her

		if(!value.is_null()) { // if we are null
			return HYP(grammar, grammar->copy_resample(value, [](const Node& n) { return n.can_resample; }));
		}
		else {
			return HYP(grammar, grammar->generate<t_output>());
		}
	}
	
	void set_value(T&  v) { value = v; }
	void set_value(T&& v) { value = v; }
	
	virtual double compute_prior() override {
		assert(grammar != nullptr && "Grammar was not initialized before trying to call compute_prior");
		
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * hypotheses */
		if(this->value.count() > MAX_NODES) {
			return this->prior = -infinity;
		}
		
		return this->prior = grammar->log_probability(value);
	}
	
	virtual double compute_single_likelihood(const t_datum& datum) override {
		// compute the likelihood of a *single* data point. 
		assert(0);// for base classes to implement, but don't set = 0 since then we can't create Hypothesis classes. 
	}

	virtual void push_program(Program& s, short k=0) override {
		assert(k==0); // this is only for lexica
		s.reserve(128); // seems to help to reserve some
		value.linearize(s);
	}


	// we defaultly map outputs to log probabilities
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err, Dispatchable<t_input,t_output>* loader, 
				unsigned long max_steps=2048, unsigned long max_outputs=256, double minlp=-10.0){
		
		VirtualMachinePool<t_input,t_output> pool(max_steps, max_outputs, minlp);

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

	virtual t_output callOne(const t_input x, const t_output err, Dispatchable<t_input,t_output>* loader=nullptr) {
		// we can use this if we are guaranteed that we don't have a stochastic hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState<t_input,t_output> vms(x, err);		
		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		return vms.run(nullptr, this, loader == nullptr? this : nullptr); // default to using "this" as the loader		
	}
	

	virtual std::string string() const override {
		return std::string("\u03BBx.") + value.string();
	}
	virtual std::string parseable() const { 
		return value.parseable(); 
	}
	static HYP from_string(Grammar& g, std::string s) {
		return HYP(g, g.expand_from_names(s));
	}
	
	
	virtual size_t hash() const override {
		return value.hash();
	}
	
	virtual bool operator==(const HYP& h) const override {
		return this->value == h.value;
	}
	
	virtual vmstatus_t dispatch_custom(Instruction i, 
								  VirtualMachinePool<t_input,t_output>* pool, 
								  VirtualMachineState<t_input,t_output>* vms,  
								  Dispatchable<t_input, t_output>* loader) override {
		assert(false && "*** To use dispatch_custom (e.g. with defined CustomOps) you must override it to process these instructions.");
	}

	
	virtual HYP copy_and_complete() const {
		// make a copy and fill in the missing nodes.
		// NOTE: here we set all of the above nodes to NOT resample
		// TODO: That part should go somewhere else eventually I think?
		HYP h(grammar, T(value));
		h.prior=0.0;h.likelihood=0.0;h.posterior=0.0; // reset these just in case
		
		const std::function<void(Node&)> myf =  [](Node& n){n.can_resample=false;};
		h.value.map(myf);
		grammar->complete(h.value);

		return h;
	}

	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const override {
		if(value.is_null()) { // if the value is null, our neighbors is the number of ways we can do nt
			auto nt = grammar->nt<t_output>();
			return grammar->count_rules(nt);
		}
		else {
			return grammar->neighbors(value);
//			 to rein in the mcts branching factor, we'll count neighbors as just the first unfilled gap
//			 we should not need to change make_neighbor since it fills in the first, first
//			return value.first_neighbors(*grammar);
		}
	}

	virtual HYP make_neighbor(int k) const override {
		HYP h(grammar); // new hypothesis
		auto nt = grammar->nt<t_output>();
		if(value.is_null()) {
			assert(k >= 0);
			assert(k < (int)grammar->count_rules(nt));
			auto r = grammar->get_rule(nt,(size_t)k);
			h.value = grammar->makeNode(r);
		}
		else {
			T t = value;
			grammar->expand_to_neighbor(t,k);
			h.value = t;
		}
		return h;
	}
	virtual bool is_evaluable() const override {
		// This checks whether it should be allowed to call "call" on this hypothesis. 
		// Usually this means that that the value is complete, meaning no partial subtrees
		return value.is_complete();
	}
	 
};
