#pragma once





//
//
//TODO: We shuold be making this one NOT have pointers for value, but rather
//      store the value
//
//
//









#include <string.h>
#include "Proposers.h"

template<typename HYP, typename T, nonterminal_t nt, typename t_input, typename t_output, typename _t_datum=default_datum<t_input, t_output>>
class LOTHypothesis : public Dispatchable<t_input,t_output>, 
				      public MCMCable<HYP,t_input,t_output,_t_datum>, // remember, this defines t_data, t_datum
					  public Searchable<HYP,t_input,t_output>,
					  public Serializable<HYP,t_input,t_output>
					  
{
	// stores values as a pointer to something of type T, whose memory I manage (I delete it when I go away)
	// This also stores a pointer to a grammar, but I do not manage its memory
	// nt store the value of the root nonterminal
	// HYP stores my own type (for subclasses) so I know how to cast copy and propose
public:     
	typedef typename MCMCable<HYP,t_input,t_output,_t_datum>::t_data t_data;
	typedef typename MCMCable<HYP,t_input,t_output,_t_datum>::t_datum t_datum;
	
	Grammar* grammar;
	T value;

	LOTHypothesis(Grammar* g=nullptr)  : grammar(g), value(NullRule,0.0,true) {}
	LOTHypothesis(Grammar* g, T&& x)   : grammar(g), value(x) {}
	LOTHypothesis(Grammar* g, T x)     : grammar(g), value(x) {}

	// parse this from a string
	LOTHypothesis(Grammar* g, std::string s) : grammar(g) {
		value = grammar->expand_from_names<Node>(s);
	}

	// copy constructor
	LOTHypothesis(const LOTHypothesis& h) : MCMCable<HYP,t_input,t_output,t_datum>(h),
		grammar(h.grammar), value(h.value) { // prior, likelihood, etc. should get copied 
	}

	LOTHypothesis(LOTHypothesis&& h) : MCMCable<HYP,t_input,t_output,t_datum>(h) {
		// move operator takes over 
		grammar = h.grammar;
		value = std::move(h.value);
	}
	
	void operator=(const LOTHypothesis& h) {
		MCMCable<HYP,t_input,t_output,t_datum>::operator=(h);
		grammar = h.grammar;
		value = h.value;
	}
	void operator=(LOTHypothesis&& h) {
		MCMCable<HYP,t_input,t_output,t_datum>::operator=(h);
		grammar = h.grammar;
		value = std::move(h.value);
	}
	
	virtual ~LOTHypothesis() {
		// don't delete anything
	}
		
	
	// Stuff to create hypotheses:
	virtual std::pair<HYP,double> propose() const {
		// tODO: Check how I do fb here?
		
		// simplest way of doing proposals
		auto x = regeneration_proposal(grammar, value);	
		return std::make_pair(HYP(this->grammar, x.first), x.second); // return HYP and fb
		
		// Just do regeneration proposals -- but require them to be unique 
		// so we can more easily count accept/reject
		/// Nooo I think we can't do this and keep detailed balance...
//		while(!CTRL_C) {
//			auto x = regeneration_proposal(grammar, value);	
//			
//			ret->set_value(x.first); // please use setters -- needed for stuff like symbolicRegression
//			fb = x.second;
//			
//			if(*ret->value == *value) delete ret->value; // keep going	
//			else					  break; // done		
//		}

//		while(true) {
//			if(uniform() < 0.5) { // p of regeneration
//				std::tie(ret->value, fb) = regeneration_proposal(grammar, value);
//			}
//			else {
//				if(uniform() < 0.5) {
//					std::tie(ret->value, fb) = insert_proposal_tree(grammar, value);
//				}
//				else {
//					std::tie(ret->value, fb) = delete_proposal_tree(grammar, value);
//				}
//			}
//			
//			if(*ret->value == *value) delete ret->value; // keep going	
//			else					  break; // done	
//		}


		// choose a type:
//		if(uniform() < 0.1) { // p of regeneration
//			std::tie(ret->value, fb) = regeneration_proposal(grammar, value);
//		}
//		else {
//			if(uniform() < 0.5) {
//				std::tie(ret->value, fb) = insert_proposal_tree(grammar, value);
//			}
//			else {
//				std::tie(ret->value, fb) = delete_proposal_tree(grammar, value);
//			}
//		}
		
//		return std::make_pair(ret, fb);
	}
	

	
	virtual HYP restart() const {
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. So her

		if(!value.is_null()) { // if we are null
			return HYP(grammar, value.copy_resample(grammar, [](const Node& n) { return n.can_resample; }));
		}
		else {
			return HYP(grammar, grammar->generate<T>(nt));
		}
	}
	
	virtual double compute_prior() {
		assert(grammar != nullptr && "Grammar was not initialized before trying to call compute_prior");
		this->prior = grammar->log_probability<T>(value);
		return this->prior;
	}
	
	virtual double compute_single_likelihood(const t_datum& datum) {
		// compute the likelihood of a *single* data point. 
		assert(0);// for base classes to implement, but don't set = 0 since then we can't create Hypothesis classes. 
	}

	virtual void push_program(Program& s, short k=0) {
		assert(k==0); // this is only for lexica
		
		value.linearize(s);
	}


	// we defaultly map outputs to log probabilities
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err, Dispatchable<t_input,t_output>* loader, 
				unsigned long max_steps=2048, unsigned long max_outputs=256, double minlp=-10.0){
		
		VirtualMachinePool<t_input,t_output> pool(max_steps, max_outputs, minlp);

		VirtualMachineState<t_input,t_output> vms(x, err);
		
		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		
		pool.push(std::move(vms)); // add vms to the pool
		
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
	

	virtual std::string string() const {
		return std::string("\u03BBx.") + value.string();
	}
	virtual std::string parseable(std::string delim=":") const { 
		return value.parseable(delim); 
	}
	virtual size_t hash() const {
		return value.hash();
	}
	
	virtual bool operator==(const LOTHypothesis<HYP,T,nt,t_input,t_output,t_datum>& h) const {
		return this->value == h.value;
	}
	
	virtual abort_t dispatch_rule(Instruction i, VirtualMachinePool<t_input,t_output>* pool, VirtualMachineState<t_input,t_output>& vms,  Dispatchable<t_input, t_output>* loader )=0;

	
	virtual HYP copy_and_complete() const {
		// make a copy and fill in the missing nodes.
		// NOTE: here we set all of the above nodes to NOT resample
		// TODO: That part should go somewhere else eventually I think?
		HYP h(grammar, value);
		h.prior=0.0;h.likelihood=0.0;h.posterior=0.0; // reset these just in case
		
		const std::function<void(Node&)> myf =  [](Node& n){n.can_resample=false;};
		h.value.map(myf);
		h.value.complete(grammar);

		return h;
	}

	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const {
		if(value.is_null()) { // if the value is null, our neighbors is the number of ways we can do nt
			return grammar->count_expansions(nt);
		}
		else {
			return value.neighbors(grammar);
//			 to rein in the mcts branching factor, we'll count neighbors as just the first unfilled gap
//			 we should not need to change make_neighbor since it fills in the first, first
//			return value.first_neighbors(*grammar);
		}
	}

	virtual HYP make_neighbor(int k) const {
		HYP h(grammar); // new hypothesis
		if(value.is_null()) {
			assert(k >= 0);
			assert(k < (int)grammar->count_expansions(nt));
			auto r = grammar->get_rule(nt,k);
			h.value = grammar->make<Node>(r);
		}
		else {
			T t = value;
			t.expand_to_neighbor(grammar,k);
			h.value = t;
		}
		return h;
	}
	virtual bool is_evaluable() const {
		// This checks whether it should be allowed to call "call" on this hypothesis. 
		// Usually this means that that the value is complete, meaning no partial subtrees
		return value.is_evaluable();
	}

//
//	virtual std::string serialize() const {
//		return std::to_string(this->prior) + SERIALIZATION_SEPERATOR + std::to_string(this->likelihood) + SERIALIZATION_SEPERATOR + this->value.parseable();
//	}
//	
//	virtual void deserialize(const std::string s) {	
//		auto x = split(s, SERIALIZATION_SEPERATOR);
//		this->prior      = std::stod(x[0]);
//		this->likelihood = std::stod(x[1]);
//		this->set_value(grammar->expand_from_names<Node>(x[2]));		
//	}


	 
};
//
//
//std::ostream& operator<<(std::ostream& os, const myclass& obj)
//{
//      os << obj.somevalue;
//      return os;
//}