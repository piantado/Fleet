#pragma once


#include <string.h>
#include "Proposers.h"
#include "Program.h"
#include "Node.h"
#include "DiscreteDistribution.h"
#include "Errors.h"

#include "Hypotheses/Interfaces/Bayesable.h"
#include "Hypotheses/Interfaces/MCMCable.h"
#include "Hypotheses/Interfaces/Searchable.h"
#include "Hypotheses/Interfaces/Callable.h"
#include "Hypotheses/Interfaces/Serializable.h"

/**
 * @class LOTHypothesis
 * @author piantado
 * @date 05/05/20
 * @file LOTHypothesis.h
 * @brief A LOTHypothesis is the basic unit for doing LOT models. It is templated with itself (the curiously recurring tempalte
 *        pattern), an input type, and output type, a grammar type, and types for the individual data elements and vector
 *        of data. Usually you will subclass this (or a Lexicon) as this_totheses in a LOT model. 
 * 		  
 * 		  The kind of virtual machinse that are called here are defined inside Grammar (even though LOTHypothesis would be a 
 *        more natural palce) because we need access to the Grammar's parameter pack over types. 
 */
template<typename this_t, 
		 typename _input_t, 
		 typename _output_t, 
		 typename _Grammar_t,
		 typename _datum_t=defaultdatum_t<_input_t, _output_t>, 
		 typename _data_t=std::vector<_datum_t>
		 >
class LOTHypothesis : public MCMCable<this_t,_datum_t,_data_t>, // remember, this defines data_t, datum_t
					  public Searchable<this_t,_input_t,_output_t>,
					  public Callable<_input_t, _output_t, typename _Grammar_t::VirtualMachineState_t>,
					  public Serializable<this_t>
{
public:     
	typedef typename Bayesable<_datum_t,_data_t>::datum_t datum_t;
	typedef typename Bayesable<_datum_t,_data_t>::data_t   data_t;
	using Callable_t = Callable<_input_t, _output_t, typename _Grammar_t::VirtualMachineState_t>;
	using Grammar_t = _Grammar_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	using VirtualMachineState_t = typename Grammar_t::VirtualMachineState_t;
	
	static const size_t MAX_NODES = 64; // max number of nodes we allow; otherwise -inf prior
	
	Grammar_t* grammar;

protected:

	Node value;
	
public:
	LOTHypothesis(Grammar_t* g=nullptr)     : MCMCable<this_t,datum_t,data_t>(), grammar(g), value(NullRule,0.0,true) {}
	LOTHypothesis(Grammar_t* g, Node&& x)   : MCMCable<this_t,datum_t,data_t>(), grammar(g), value(x) {}
	LOTHypothesis(Grammar_t* g, Node& x)    : MCMCable<this_t,datum_t,data_t>(), grammar(g), value(x) {}

	// parse this from a string
	LOTHypothesis(Grammar_t* g, std::string s) : MCMCable<this_t,datum_t,data_t>(), grammar(g)  {
		value = grammar->from_parseable(s);
	}
	
	[[nodiscard]] virtual std::pair<this_t,double> propose() const override {
		/**
		 * @brief Default proposal is rational-rules style regeneration. 
		 * @return 
		 */
	
		assert(grammar != nullptr);

		// simplest way of doing proposals
		auto x = Proposals::regenerate(grammar, value);	
		
		// return a pair of Hypothesis and forward-backward probabilities
		return std::make_pair(this_t(this->grammar, std::move(x.first)), x.second); // return this_t and fb
	}	

	
	[[nodiscard]] virtual this_t restart() const override {
		/**
		 * @brief This is used to restart chains, sampling from prior
		 * @return 
		 */
		
		assert(grammar != nullptr);
		
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. 

		if(not value.is_null()) { // if we are null
			return this_t(this->grammar, this->grammar->copy_resample(value, [](const Node& n) { return n.can_resample; }));
		}
		else {
			return this_t(this->grammar, this->grammar->generate());
		}
	}
	
		  Node& get_value()       {	return value; }
	const Node& get_value() const {	return value; }
	
	void set_value(Node&  v) { value = v; }
	void set_value(Node&& v) { value = v; }
	
	virtual double compute_prior() override {
		assert(grammar != nullptr && "Grammar was not initialized before trying to call compute_prior");
		
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * this_totheses */
		if(this->value.count() > MAX_NODES) {
			return this->prior = -infinity;
		}
		
		return this->prior = grammar->log_probability(value);
	}
	
	virtual double compute_single_likelihood(const datum_t& datum) override {
		// compute the likelihood of a *single* data point. 
		throw NotImplementedError("*** You must define compute_single_likelihood");// for base classes to implement, but don't set = 0 since then we can't create Hypothesis classes. 
	}           

//	virtual size_t program_size(short s) override {
//		assert(s == 0 && "*** You shouldn't be passing nonzero s to a LOTHypothesis because it does nothing!");
//		return value.program_size();
//	}

	virtual void push_program(Program& s, short k=0) override {
		assert(k==0 && "*** the short argument in push_program should only be nonzero for lexica");
		//s.reserve(s.size() + value.program_size()+1);
		value.template linearize<Grammar_t>(s);
	}

	virtual std::string string(std::string prefix="") const override {
		return this->string(prefix,true);
	}
	virtual std::string string(std::string prefix, bool usedot) const {
		return prefix + std::string("\u03BBx.") + value.string(usedot);
	}
	virtual std::string serialize() const { 
		return value.serialize(); 
	}
	static this_t deserialize(std::string& s) { 
		return grammar->from_serialized(s);
	}
	
	static this_t from_string(Grammar_t* g, std::string s) {
		return this_t(g, g->from_parseable(s));
	}
	
	virtual size_t hash() const override {
		return value.hash();
	}
	
	virtual bool operator==(const this_t& h) const override {
		return this->value == h.value;
	}
	
	virtual void complete() override {
		if(value.is_null()) {
			auto nt = grammar->template nt<output_t>();
			value = grammar->generate(nt);
		}
		else {
			grammar->complete(value);
		}
	}

	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const override {
		if(value.is_null()) { // if the value is null, our neighbors is the number of ways we can do nt
			auto nt = grammar->template nt<output_t>();
			return grammar->count_rules(nt);
		}
		else {
			return grammar->neighbors(value);
//			 to rein in the mcts branching factor, we'll count neighbors as just the first unfilled gap
//			 we should not need to change make_neighbor since it fills in the first, first
		}
	}

	virtual void expand_to_neighbor(int k) override {
		assert(grammar != nullptr);
		
		if(value.is_null()){
			auto nt = grammar->template nt<output_t>();
			auto r = grammar->get_rule(nt,(size_t)k);
			value = grammar->makeNode(r);
		}
		else {			
			grammar->expand_to_neighbor(value, k);
		}
	}
	
	virtual double neighbor_prior(int k) override {
		// What is the prior for this neighbor?
		if(value.is_null()){
			auto nt = grammar->template nt<output_t>();
			auto r = grammar->get_rule(nt,(size_t)k);
			return log(r->p)-log(grammar->rule_normalizer(r->nt));
		}
		else {			
			return grammar->neighbor_prior(value, k);
		}
	}
	
	virtual bool is_evaluable() const override {
		// This checks whether it should be allowed to call "call" on this Hypothesis. 
		// Usually this means that that the value is complete, meaning no partial subtrees
		return value.is_complete();
	}
	
	 /**
	  * @brief Count up how many times I use recursion -- we keep a list of recursion here
	  * @return 
	  */ 
	size_t recursion_count() {
		size_t cnt = 0;
		for(auto& n : value) {
			cnt +=  n.rule->is_a(Op::Recurse) + 
					n.rule->is_a(Op::MemRecurse) +
					n.rule->is_a(Op::SafeRecurse) +
					n.rule->is_a(Op::SafeMemRecurse);
		}
		return cnt;
	} 
	 
};

