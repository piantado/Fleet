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
#include "Hypotheses/Interfaces/Serializable.h"

/**
 * @class LOTHypothesis
 * @author piantado
 * @date 05/05/20
 * @file LOTHypothesis.h
 * @brief A LOTHypothesis is the basic unit for doing LOT models. It store a Node as its value, and handles all of the proposing
 *        and computing priors, likelihoods, etc. It compiles this Node into a "program" which is used to make function calls,
 * 		  which means that the value should only be changed via LOTHypothesis::set_value. 
 * 
 * 		  LOTHypothsis is templated with itself (the curiously recurring tempalte
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
		 _Grammar_t* grammar,
		 typename _datum_t=defaultdatum_t<_input_t, _output_t>, 
		 typename _data_t=std::vector<_datum_t>,
		 typename _VirtualMachineState_t=_Grammar_t::VirtualMachineState_t
		 >
class LOTHypothesis : public MCMCable<this_t,_datum_t,_data_t>, // remember, this defines data_t, datum_t
					  public Searchable<this_t,_input_t,_output_t>,
					  public Serializable<this_t>,
					  public ProgramLoader<_VirtualMachineState_t> 
{
public:     

	typedef typename Bayesable<_datum_t,_data_t>::datum_t datum_t;
	typedef typename Bayesable<_datum_t,_data_t>::data_t   data_t;
	using Grammar_t = _Grammar_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	using VirtualMachineState_t = _VirtualMachineState_t;
	
	// this splits the prior,likelihood,posterior,and value
	static const char SerializationDelimiter = '\t';
	
	static const size_t MAX_NODES = 64; // max number of nodes we allow; otherwise -inf prior
	
	// store the the total number of instructions on the last call
	// (summed up for stochastic, else just one for deterministic)
	unsigned long total_instruction_count_last_call;
	unsigned long total_vms_steps;
	
	// A Callable stores its program
	Program<VirtualMachineState_t> program;
	
protected:

	Node value;
	
public:

	LOTHypothesis()           : MCMCable<this_t,datum_t,data_t>(), value(NullRule,0.0,true) {	
	}
	
	LOTHypothesis(Node&& x)   : MCMCable<this_t,datum_t,data_t>() {
		set_value(x);
	}
	
	LOTHypothesis(Node& x)    : MCMCable<this_t,datum_t,data_t>() {
		set_value(x);
	}

	// parse this from a string
	LOTHypothesis(std::string s) : MCMCable<this_t,datum_t,data_t>() {
		set_value(grammar->from_parseable(s));
	}
	
	LOTHypothesis(const LOTHypothesis& c) {
		this->operator=(c); // copy all this garbage -- not sure what to do here
	}

	LOTHypothesis(const LOTHypothesis&& c) {
		this->operator=(std::move(c)); 
	}

	LOTHypothesis& operator=(const LOTHypothesis& c) {
		MCMCable<this_t,_datum_t,_data_t>::operator=(c);
		
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = c.program; 
		value = c.value; // no setting -- just copy the program
		if(c.program.loader == &c) 
			program.loader = this;  // something special here -- if c's loader was itself, make this myself; else leave it
		return *this;
	}

	LOTHypothesis& operator=(const LOTHypothesis&& c) {
		MCMCable<this_t,_datum_t,_data_t>::operator=(c);
		
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = c.program; //std::move(c.program); 
		value = c.value; // std::move(c.value); // no setting -- just copy the program
		if(c.program.loader == &c) 
			program.loader = this;  // something special here -- if c's loader was itself, make this myself; else leave it
		return *this;
	}	
	
	/**
	 * @brief Default proposal is rational-rules style regeneration. 
	 * @return 
	 */
	[[nodiscard]] virtual std::pair<this_t,double> propose() const override {

		// simplest way of doing proposals
		auto x = Proposals::regenerate(grammar, value);	
		
		// return a pair of Hypothesis and forward-backward probabilities
		return std::make_pair(this_t(std::move(x.first)), x.second); // return this_t and fb
	}	

	[[nodiscard]] static this_t sample() {
		return this_t(grammar->generate());
	}

	/**
	 * @brief This is used to restart chains, sampling from prior but ONLY for nodes that are can_resample
	 * @return 
	 */	
	[[nodiscard]] virtual this_t restart() const override {
		
		// This is used in MCMC to restart chains 
		// this ordinarily would be a resample from the grammar, but sometimes we have can_resample=false
		// and in that case we want to leave the non-propose nodes alone. 

		if(not value.is_null()) { // if we are null
			return this_t(grammar->copy_resample(value, [](const Node& n) { return n.can_resample; }));
		}
		else {
			return this_t(grammar->generate());
		}
	}
	
		  Node& get_value()       {	return value; }
	const Node& get_value() const {	return value; }
		
	/**
	 * @brief Set the value to v. (NOTE: This compiles into a program)
	 * @param v
	 */
	void set_value(Node&  v) { 
		value = v; 
		this->compile(); // compile with myself defaultly as a loader
	}
	void set_value(Node&& v) { 
		value = v;
		this->compile();
	}
	
	Grammar_t* get_grammar() const { return grammar; }
	
	/**
	 * @brief Compute the prior -- defaultly just the PCFG (grammar) prior
	 * @return 
	 */	
	virtual double compute_prior() override {
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

	void compile() {
		this->program.clear();
		value.template linearize<VirtualMachineState_t, Grammar_t>(this->program);
		this->program.loader = this; // program loader defaults to myself
	}

	/**
	 * @brief This puts the code from my node onto s. Used internally in e.g. recursion
	 * @param s
	 * @param k
	 */
	virtual void push_program(Program<VirtualMachineState_t>& s) override {
		for(auto it = this->program.begin(); it != this->program.end(); it++) {
			s.push(*it);
		}		
	}

	virtual std::string string(std::string prefix="") const override {
		return this->string(prefix,true);
	}
	virtual std::string string(std::string prefix, bool usedot) const {
		return prefix + std::string("\u03BBx.") + value.string(usedot);
	}

	static this_t from_string(Grammar_t* g, std::string s) {
		return this_t(g, g->from_parseable(s));
	}
	
	virtual size_t hash() const override {
		return value.hash();
	}
	
	/**
	 * @brief Equality is checked on equality of values; note that greater-than is still on posteriors. 
	 * @param h
	 * @return 
	 */	
	virtual bool operator==(const this_t& h) const override {
		return this->value == h.value;
	}
	
	/**
	 * @brief Modify this hypothesis's value by filling in all the gaps. 
	 */	
	virtual void complete() override {
		if(value.is_null()) {
			auto nt = grammar->template nt<output_t>();
			set_value(grammar->generate(nt));
		}
		else {
			grammar->complete(value);
			set_value(value); // this will compile it
		}
	}

	/**
	 * @brief A variant of call that assumes no stochasticity and therefore outputs only a single value. 
	 * 		  (This uses a nullptr virtual machine pool, so will throw an error on flip)
	 * @param x
	 * @param err
	 * @return 
	 */
	virtual output_t callOne(const input_t x, const output_t& err=output_t{}) {
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
					  std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			
			if(program.empty()) PRINTN(">>", string(), program.size());
						  
			assert(not program.empty());
			
			// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
			// the savings is that we don't have to create a VirtualMachinePool		
			VirtualMachineState_t vms(x, err, nullptr);		

			vms.program = program; // write my program into vms (program->loader is used for everything else)
			
			const auto out = vms.run(); 	
			total_instruction_count_last_call = vms.runtime_counter.total;
			total_vms_steps = 1;
			
			return out;
			
		} else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }
	}
	
	/**
	 * @brief Run the virtual machine on input x, and marginalize over execution paths to return a distribution
	 * 		  on outputs. Note that loader must be a program loader, and that is to handle recursion and 
	 *        other function calls. 
	 * @param x - input
	 * @param err - output value on error
	 * @param loader - where to load recursive calls
	 * @return 
	 */	
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t& err=output_t{}) {
		
		// The below condition is needed in case we create e.g. a lexicon whose input_t and output_t differ from the VirtualMachineState
		// in that case, these functions are all overwritten and must be called on their own. 
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
				      std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			assert(not program.empty());
		    
			VirtualMachinePool<VirtualMachineState_t> pool; 		
			
			VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, &pool);	
			
			vms->program = program; // copy our program into vms
			
			pool.push(vms); // put vms into the pool
			
			const auto out = pool.run();	
			
			// update some stats
			total_instruction_count_last_call = pool.total_instruction_count;
			total_vms_steps = pool.total_vms_steps;
			
			return out;
			
	  } else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }
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
		//assert(grammar != nullptr);
		
		if(value.is_null()){
			auto nt = grammar->template nt<output_t>();
			auto r = grammar->get_rule(nt,(size_t)k);
			value = grammar->makeNode(r);
		}
		else {			
			grammar->expand_to_neighbor(value, k); // NOTE that if we do this, we have to compile still...
		}
		
		if(value.is_complete()) compile(); // this seems slow/wasteful, but I'm not sure of the alternative. 
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
	
	/**
	 * @brief A node is "evaluable" if it is complete (meaning no null subnodes)
	 * @return 
	 */	
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
	
	/**
	 * @brief Convert this into a string which can be written to a file. 
	 * @return 
	 */	
	virtual std::string serialize() const override { 
		// NOTE: This doesn't preseve everything (it doesn't save can_propose, for example)
		return str(this->prior)      + SerializationDelimiter + 
		       str(this->likelihood) + SerializationDelimiter +
			   str(this->posterior)  + SerializationDelimiter +
			   value.parseable(); 
	}
	
	/**
	 * @brief Convert this from a string which was in a file. 
	 * @param s
	 * @return 
	 */	
	static this_t deserialize(const std::string& s) { 
		auto [pr, li, po, v] = split<4>(s, SerializationDelimiter);
		
		auto h = this_t(grammar->from_parseable(v));
	
		// restore the bayes stats
		h.prior = string_to<double>(pr); 
		h.likelihood = string_to<double>(li); 
		h.posterior = string_to<double>(po);
		
		return h; 
	}
	
	 
};

