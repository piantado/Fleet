#pragma once

#include <string>
#include <span>

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
template<typename this_t, // NOTE: IF YOU CHANGE THESE, CHANGE IN DeterministicLOTHypothesis and StochasticLOTHypothesis
		 typename _input_t, 
		 typename _output_t, 
		 typename _Grammar_t,
		 _Grammar_t* grammar,
		 typename _datum_t=defaultdatum_t<_input_t, _output_t>, 
		 typename _data_t=std::span<_datum_t>,
		 typename _VirtualMachineState_t=typename _Grammar_t::VirtualMachineState_t
		 >
class LOTHypothesis : public MCMCable<this_t,_datum_t,_data_t>, // remember, this defines data_t, datum_t
					  public Searchable<this_t,_input_t,_output_t>,
					  public Serializable<this_t>,
					  public ProgramLoader<_VirtualMachineState_t>
{
public:     

	typedef typename Bayesable<_datum_t,_data_t>::datum_t datum_t;
	typedef typename Bayesable<_datum_t,_data_t>::data_t   data_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	
	// sometimes the grammar input/output types differ from my own
	using Grammar_t = _Grammar_t;
	using ginput_t   = typename _Grammar_t::input_t;
	using goutput_t  = typename _Grammar_t::output_t;
	
	using VirtualMachineState_t = _VirtualMachineState_t;
	
	// proposals return this:
	using ProposalType = std::optional<std::pair<this_t,double>>;
	
	// this splits the prior,likelihood,posterior,and value
	static const char SerializationDelimiter = Serialization::HypothesisDelimiter;
	
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
	
	LOTHypothesis(Node& x)    : MCMCable<this_t,datum_t,data_t>() {
		set_value(x);
	}

	LOTHypothesis(Node&& x)   : MCMCable<this_t,datum_t,data_t>() {
		set_value(std::move(x)); // forward the rvalue-ness; x is an lvalue in here
	}
	
	// parse this from a string
	LOTHypothesis(std::string s) : MCMCable<this_t,datum_t,data_t>() {
		set_value(grammar->from_parseable(s));
	}
	
	LOTHypothesis(const LOTHypothesis& c) {
		this->operator=(c); // copy all this garbage -- not sure what to do here
	}

	LOTHypothesis(LOTHypothesis&& c) {
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

	LOTHypothesis& operator=(LOTHypothesis&& c) {
		// NOTE: this must NOT take (const LOTHypothesis&&) -- a const rvalue cannot actually
		// be moved from, so the old version deep-copied the program and the whole value tree
		// on every "move" (e.g. on every MCMC accept and every parallel-tempering swap).
		MCMCable<this_t,_datum_t,_data_t>::operator=(c); // bases hold scalars; copying them is cheap

		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;

		// something special here -- if c's loader was itself, make this myself; else leave it
		// (must be checked *before* we move the program out of c)
		const bool self_loader = (c.program.loader == &c);

		program = std::move(c.program); // moves the instruction stack; keeps the loader pointer, fixed below
		value   = std::move(c.value);   // moves the tree -- no re-compile needed, the program already matches

		if(self_loader)
			program.loader = this;
		return *this;
	}
	
	
	/**
	 * @brief Default proposal is rational-rules style regeneration. 
	 * @return 
	 */
	[[nodiscard]] virtual ProposalType propose() const override {

		// simplest way of doing proposals
		auto p = Proposals::regenerate(grammar, value);
		if(not p) return {}; // if failed

		this_t ret = *static_cast<const this_t*>(this); // copy so we copy everything
		ret.set_value(std::move(p->first)); // move the proposed tree in rather than copying it

		// return a pair of Hypothesis and forward-backward probabilities
		return std::make_pair(std::move(ret), p->second); // return this_t and fb
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
			return this_t::sample(); //this_t(grammar->generate());
		}
	}
	
		  Node& get_value()       {	return value; }
	const Node& get_value() const {	return value; }
		
	/**
	 * @brief Set the value to v. (NOTE: This compiles into a program)
	 * @param v
	 */
	void set_value(Node&  v, bool should_compile=true) { 
		value = v; 
		if(should_compile) 
			this->compile(); // compile with myself defaultly as a loader
	}
	void set_value(Node&& v, bool should_compile=true) {
		value = std::move(v); // v is an lvalue in here; without std::move this overload silently copied
		if(should_compile)
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
		if(this->value.count() > FleetArgs::MAX_NODES) {
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
		this->was_called = true; // by definition we should be setting this if we're a program loader

		// bulk-append the instructions (identical order to pushing them one at a time,
		// but a single vector insert instead of per-instruction push_backs)
		s.append(this->program);
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
	 * @brief Modify this hypothesis's value by (randomly) filling in all the gaps. 
	 */	
	virtual void complete() override {
		if(value.is_null()) {
			auto nt = grammar->template nt<goutput_t>();
			set_value(grammar->generate(nt));
		}
		else {
			grammar->complete(value);
			set_value(value); // this will compile it
		}
	}

	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // The main complication with these is that they handle nullptr 
	 
	virtual int neighbors() const override {
		if(value.is_null()) { // if the value is null, our neighbors is the number of ways we can do nt
			auto nt = grammar->template nt<goutput_t>();
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
			auto nt = grammar->template nt<goutput_t>();
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
			auto nt = grammar->template nt<goutput_t>();
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
			cnt +=  n.rule->is_recursive();
		}
		return cnt;
	} 	
	
	/**
	 * @brief Convert this into a string which can be written to a file:
	 *        prior \t likelihood \t posterior \t value.parseable()
	 *        NOTE: This doesn't preserve everything: not can_propose, born, or born_chain_idx, and the
	 *        scores are stored as they are (deserialize does not recompute them, so they are only right
	 *        for the data/grammar they were computed with -- see TopN::compute_posterior to rescore).
	 *        Subclasses with extra state (e.g. ConstantContainer) must override BOTH this and deserialize.
	 * @return 
	 */	
	virtual std::string serialize() const override { 
		// NOTE: exact round-trip of the doubles relies on cppstr's str(double), which formats
		// the shortest representation that parses back bit-identically (see parse_exact)
		return str(this->prior)      + SerializationDelimiter + 
		       str(this->likelihood) + SerializationDelimiter +
			   str(this->posterior)  + SerializationDelimiter +
			   value.parseable(); 
	}
	
	/**
	 * @brief Convert this from a string which was in a file. Throws DeserializationError on bad input.
	 *        We split only at the first three delimiters, so rule formats may contain tabs.
	 * @param s
	 * @return 
	 */	
	static this_t deserialize(const std::string& s) { 
		const auto f = split_first(s, SerializationDelimiter, 3, "LOTHypothesis");

		Node v = grammar->from_parseable(f[3]);

		// A partial tree (with null gaps, e.g. from MCTS) is serialized fine, but the Node constructor
		// compiles, which asserts on gaps -- so set those without compiling (as expand_to_neighbor does)
		this_t h = [&]() -> this_t {
			if(v.is_complete()) {
				return this_t(std::move(v));
			}
			else if constexpr (std::is_default_constructible_v<this_t>) {
				this_t p;
				p.set_value(std::move(v), false);
				return p;
			}
			else {
				throw DeserializationError("*** Cannot deserialize a partial hypothesis into a type without a default constructor: " + s);
			}
		}();

		// restore the bayes stats
		h.prior      = parse_exact<double>(f[0], "prior"); 
		h.likelihood = parse_exact<double>(f[1], "likelihood"); 
		h.posterior  = parse_exact<double>(f[2], "posterior");
		
		return h; 
	}
	
	 
};
