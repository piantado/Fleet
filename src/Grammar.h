#pragma once

#include <tuple>
#include <deque>
#include <exception>
#include "IO.h"

#include "Node.h"
#include "Random.h"

// an exception for recursing too deep so we can print a trace of what went wrong
class DepthException: public std::exception {} depth_exception;


template<typename T, typename... args> // function type
struct Primitive ;

class Grammar {
	/* 
	 * A grammar stores all of the rules associated with any kind of nonterminal and permits us
	 * to sample as well as compute log probabilities. 
	 * A grammar also is nwo required to store them in a fixed (sorted) order that is guaranteed
	 * not to change, adn that puts terminals first and (lower priority) high probability first
	 * as determined by Rule's sort order.
	 * 
	 * Note that Primitives are used to initialize a grammar, and they get "parsed" by Gramar.add
	 * to store them in the right places according to their return types (the index comes from
	 * the index of each type in FLEET_GRAMMAR_TYPES).
	 * The trees that Grammar generates use nt<T>() -> size_t to represent types, not the types
	 * themselves. 
	 * The trees also use Primitive.op (a size_t) to represent operations
	 */
	// how many nonterminal types do we have?
	static constexpr size_t N_NTs = std::tuple_size<std::tuple<FLEET_GRAMMAR_TYPES>>::value;

protected:
	std::vector<Rule> rules[N_NTs];
	double	  	      Z[N_NTs]; // keep the normalizer handy for each nonterminal
	
public:

	// This function converts a type (passed as a template parameter) into a 
	// size_t index for which one it in in FLEET_GRAMMAR_TYPES. 
	// This is used so that a Rule doesn't need type subclasses/templates, it can
	// store a type as e.g. nt<double>() -> size_t 
	template <class T>
	constexpr nonterminal_t nt() {
		return TypeIndex<T, std::tuple<FLEET_GRAMMAR_TYPES>>::value;
	}

	Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			Z[i] = 0.0;
		}
	}
	
	template<typename... T>
	Grammar(std::tuple<T...> tup) : Grammar() {
		add(tup, std::make_index_sequence<sizeof...(T)>{});
	}	
	
//	template<typename T>
//	Grammar(std::vector<T> prims) : Grammar() {
////		for(auto& a : primes) {
////			add(a);
//	}	
	
	Grammar(const Grammar& g) = delete; // should not be doing these
	Grammar(const Grammar&& g) = delete; // should not be doing these
	
	size_t count_nonterminals() const {
		return N_NTs;
	}
	
	size_t count_rules(const nonterminal_t nt) const {
		return rules[nt].size();
	}	
	size_t count_rules() const {
		size_t n=0;
		for(size_t i=0;i<N_NTs;i++) {
			n += count_rules((nonterminal_t)i);
		}
		return n;
	}
	size_t count_terminals(nonterminal_t nt) const {
		size_t n=0;
		for(auto& r : rules[nt]) {
			if(r.N == 0) n++;
		}
		return n;
	}
	size_t count_nonterminals(nonterminal_t nt) const {
		size_t n=0;
		for(auto& r : rules[nt]) {
			if(r.N != 0) n++;
		}
		return n;
	}
	
	size_t count_expansions(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return rules[nt].size(); 
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Methods for getting rules by some info
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	size_t get_index_of(const Rule* r) const {
		// within an index, what index are we?
		for(size_t i=0;i<rules[r->nt].size();i++) {
			if(rules[r->nt][i] == *r) { 
				return i;
			}
		}
		assert(false && "*** Did not find rule in get_index_of.");
	}
	
	Rule* get_rule(const nonterminal_t nt, size_t k) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		assert(k < rules[nt].size());
		return const_cast<Rule*>(&rules[nt][k]);
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const CustomOp o, const int a=0) {
		for(auto& r: rules[nt]) {
			if(r.instr.is_a(o) && r.instr.arg == a) 
				return &r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const BuiltinOp o, const int a=0) {
		for(auto& r: rules[nt]) {
			if(r.instr.is_a(o) && r.instr.arg == a) 
				return &r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, size_t i) {
		assert(i <= rules[nt].size());
		return &rules[nt][i];
	}
	
	virtual Rule* get_rule(const std::string s) const {
		// returns the rule for which s is a prefix -- but throws errors
		// if there aren't enough
		Rule* ret = nullptr;
		for(size_t nt=0;nt<N_NTs;nt++) {
			for(auto& r: rules[nt]) {
				if(is_prefix(s, r.format)){
					if(ret != nullptr) {
						CERR "*** Multiple rules found matching " << s TAB r.format ENDL;
						assert(0);
					}
					ret = const_cast<Rule*>(&r);
				} 
			}
		}
		
		if(ret != nullptr) { return ret; }
		else {			
			CERR "*** No rule found to match " TAB s ENDL;
			assert(0);
		}
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Sampling rules
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	double rule_normalizer(const nonterminal_t nt) const {
		assert(nt >= 0);
		assert(nt < N_NTs);
		return Z[nt];
	}

	virtual Rule* sample_rule(const nonterminal_t nt) const {
		std::function<double(const Rule& r)> f = [](const Rule& r){return r.p;};
		assert(rules[nt].size() > 0 && "*** You are trying to sample from a nonterminal with no rules!");
		return sample<Rule,std::vector<Rule>>(rules[nt], f).first; // ignore the probabiltiy 
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Computing log probabilities and priors
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	std::vector<size_t> get_counts(const Node& node) const {
		// returns a vector of counts of how often each rule was used, in a *standard* order
		// given by iterating over nts and then iterating over rules 
		
		const size_t R = count_rules(); 
		
		std::vector<size_t> out(R,0.0);
		
		// NOTE: This is an inefficiency because we build this up each time
		// We had a version of grammar that cached this, but it was complex and ugly
		// so I think we'll take the small performance hit and put it all in here
		const size_t NT = count_nonterminals();
		size_t rule_cumulative[NT]; // how many rules are there before this (in our ordering)
		rule_cumulative[0] = 0;
		for(size_t nt=1;nt<NT;nt++) {
			rule_cumulative[nt] = rule_cumulative[nt-1] + count_rules( nonterminal_t(nt-1) );
		}
		
		for(auto& n : node) {
			// now increment out, accounting for the number of rules that must have come before!
			out[rule_cumulative[n.rule->nt] + get_index_of(n.rule)] += 1;
		}
		
		return out;
	}
	
	double log_probability(Node& n) const {
		
		double lp = 0.0;		
		for(auto& x : n) {
			lp += log(x.rule->p) - log(rule_normalizer(x.rule->nt));
		}
	
		return lp;		
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of converting strings to nodes 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
	Node expand_from_names(std::deque<std::string>& q) const {
		// expands an entire stack using nt as the nonterminal -- this is needed to correctly
		// fill in Node::nulldisplay
		assert(!q.empty() && "*** Should not ever get to here with an empty queue -- are you missing arguments?");
		
		std::string pfx = q.front(); q.pop_front();
		assert(pfx != NullRule->format && "NullRule not supported in expand_from_names");

		// otherwise find the matching rule
		Rule* r = this->get_rule(pfx);
		
		Node v = makeNode(r);
		for(size_t i=0;i<r->N;i++) {
			
			if(r->child_types[i] != v.child[i].rule->nt) {
				CERR "*** Grammar expected type " << r->child_types[i] << 
					 " but got type " << v.child[i].rule->nt << " at " << 
					 r->format << " argument " << i ENDL;
				assert(false && "Bad names in expand_from_names."); // just check that we didn't miss this up
			}
			
			v.child[i] = expand_from_names(q);
		}
		return v;
	}

	Node expand_from_names(std::string s) const {
		std::deque<std::string> stk = split(s, ':');    
        return expand_from_names(stk);
	}
	
	Node expand_from_names(const char* c) const {
		std::string s = c;
        return expand_from_names(s);
	}
	
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Enumeration -- via encoding into integers
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	Node expand_from_integer(nonterminal_t nt, enumerationidx_t z) const {
		// this is a handy function to enumerate trees produced by the grammar. 
		// This works by encoding each tree into an integer using some "pairing functions"
		// (see https://arxiv.org/pdf/1706.04129.pdf)
		// Specifically, here is how we encode a tree: if it's a terminal,
		// encode the index of the terminal in the grammar. 
		// If it's a nonterminal, encode the rule with a modular pairing function
		// and then use Rosenberg-Strong encoding to encode each of the children. 
		// In this way, we can make a 1-1 pairing between trees and integers,
		// and therefore store trees as integers in a reversible way
		// as well as enumerate by giving this integers. 
		
		enumerationidx_t numterm = count_terminals(nt);
		if(z < numterm) {
			return makeNode(this->get_rule(nt, z));	// whatever terminal we wanted
		}
		else {
			auto u =  mod_decode(z-numterm, count_nonterminals(nt));
			
			Rule* r = this->get_rule(nt, u.first+numterm); // shift index from terminals (because they are first!)
			Node out = makeNode(r);
			enumerationidx_t rest = u.second; // the encoding of everything else
			for(size_t i=0;i<r->N;i++) {
				enumerationidx_t zi; // what is the encoding for the i'th child?
				if(i<r->N-1) { 
					auto ui = rosenberg_strong_decode(rest);
					zi = ui.first;
					rest = ui.second;
				}
				else {
					zi = rest;
				}
				out.child[i] = expand_from_integer(r->child_types[i], zi); // since we are by reference, this should work right
			}
			return out;		
			
		}

	}

	enumerationidx_t compute_enumeration_order(const Node& n) {
		// inverse of the above function -- what order would we be enumerated in?
		if(n.child.size() == 0) {
			return get_index_of(n.rule);
		}
		else {
			// here we work backwards to encode 
			nonterminal_t nt = n.rule->nt;
			enumerationidx_t numterm = count_terminals(nt);
			enumerationidx_t z = compute_enumeration_order(n.child[n.rule->N-1]);
			for(int i=n.rule->N-2;i>=0;i--) {
				z = rosenberg_strong_encode(compute_enumeration_order(n.child[i]),z);
			}
			z = numterm + mod_encode(get_index_of(n.rule)-numterm, z, count_nonterminals(nt));
			return z;
		}
	}
	

//
//	Node lempel_ziv_enumeration(Node* root, nonterminal_t nt, enumerationidx_t z) const {
//		// NOTE: This is no longer unique...
//		
//		enumerationidx_t numterm = count_terminals(nt);
//		if(z < numterm) {
//			return makeNode(this->get_rule(nt, z));	// whatever terminal we wanted
//		}
//		else if(root != nullptr and z < numterm + root->count() - 1) {
//			// a reference to a prior node
//			auto it = root->begin();
//		//	CERR (z-numterm-1) TAB root->string() ENDL;
//			
//			// HMM root->begin() is hard with partial trees because it goes to a NullNode
//			
//			if(z > numterm)	// bc not signed		
//				it = it + (size_t)(z-numterm-1);
//			
//			
//			
//			// TODO: THIS MUST BE CHANGED SINCE *IT WILL CONTAIN HOLES -- MUST RECURSE DOWN THEM TOO!
//			
//			return Node(*it); // copy what we got there 
//		}
//		else {
//			
//			size_t rc = (root == nullptr ? 0 : root->count()-1);
//			
//			auto u =  mod_decode(z-numterm-rc, count_nonterminals(nt));
//			
//			Rule* r = this->get_rule(nt, u.first+numterm); // shift index from terminals (because they are first!)
//			Node out = makeNode(r);
//			
//			if(root == nullptr) root = &out; // set this if its not done yet
//			
//			enumerationidx_t rest = u.second; // the encoding of everything else
//			for(size_t i=0;i<r->N;i++) {
//				enumerationidx_t zi; // what is the encoding for the i'th child?
//				if(i<r->N-1) { 
//					auto ui = rosenberg_strong_decode(rest);
//					zi = ui.first;
//					rest = ui.second;
//				}
//				else {
//					zi = rest;
//				}
//				out.child[i] = fancy_index(root, r->child_types[i], zi); // since we are by reference, this should work right
//			}
//			return out;		
//			
//		}
//
//	}
//		
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Generation
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	

	
	virtual void add(Rule&& r) {
		
		nonterminal_t nt = r.nt;
		//rules[r.nt].push_back(r);
		
		auto pos = std::lower_bound( rules[nt].begin(), rules[nt].end(), r);
		rules[nt].insert( pos, r ); // put this before
		
		Z[nt] += r.p; // keep track of the total probability
	}
	
	// recursively add a bunch of rules in a tuple -- called via
	// Grammar(std::tuple<T...> tup)
	template<typename... args, size_t... Is>
	void add(std::tuple<args...> t, std::index_sequence<Is...>) {
		(add(std::get<Is>(t)), ...);
	}
	
	template<typename T, typename... args>
	void add(Primitive<T, args...> p, const int arg=0) {
		// add a single primitive -- unpacks the types to put the rule into the right place
		add(Rule(nt<T>(), p.op, p.format, {nt<args>()...}, p.p, arg));
	}
	
	
	Node makeNode(const Rule* r) const {
		return Node(r, log(r->p)-log(Z[r->nt]));
	}
	

	Node generate(const nonterminal_t nt, unsigned long depth=0) const {
		// Sample a rule and generate from this grammar. This has a template to avoid a circular dependency
		// and allow us to generate other kinds of things from rules if we want. 
		// We use exceptions here just catch depth exceptions so we can easily get a trace of what
		// happened
		
		if(depth >= Fleet::GRAMMAR_MAX_DEPTH) {
			throw depth_exception; //assert(0);
		}
		
		Rule* r = sample_rule(nt);
		Node n = makeNode(r);
		for(size_t i=0;i<r->N;i++) {
			try{
				n.set_child(i, generate(r->child_types[i], depth+1)); // recurse down
			} catch(DepthException& e) {
				CERR "*** Grammar has recursed beyond Fleet::GRAMMAR_MAX_DEPTH (Are the probabilities right?). nt=" << nt << " d=" << depth TAB n.string() ENDL;
				throw e;
			}
		}
		return n;
	}	
	
	// a wrapper so we can call by type	
	template<class t>
	Node generate(unsigned long depth=0) {
		return generate(nt<t>(),depth);
	}
	
	Node copy_resample(const Node& node, bool f(const Node& n)) const {
		// this makes a copy of the current node where ALL nodes satifying f are resampled from the grammar
		// NOTE: this does NOT allow f to apply to nullptr children (so cannot be used to fill in)
		if(f(node)){
			return generate(node.rule->nt);
		}
		else {
		
			// otherwise normal copy
			Node ret = node;
			for(size_t i=0;i<ret.child.size();i++) {
				ret.set_child(i, copy_resample(ret.child[i], f));
			}
			return ret;
		}
	}
		
		
	/********************************************************
	 * Enumeration
	 ********************************************************/


	size_t neighbors(const Node& node) const {
		// How many neighbors do I have? We have to find every gap (nullptr child) and count the ways to expand each
		size_t n=0;
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child[i].is_null()) {
				return count_expansions(node.rule->child_types[i]); // NOTE: must use rule->child_types since child[i]->rule->nt is always 0 for NullRules
			}
			else {
				return neighbors(node.child[i]);
			}
		}
		return n;
	}

	void expand_to_neighbor(Node& node, int& which) {
		// here we find the neighbor indicated by which and expand it into the which'th neighbor
		// to do this, we loop through until which is less than the number of neighbors,
		// and then it must specify which expansion we want to take. This means that when we
		// skip a nullptr, we have to subtract from it the number of neighbors (expansions)
		// we could have taken. 
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child[i].is_null()) {
				int c = count_expansions(node.rule->child_types[i]);
				if(which >= 0 && which < c) {
					auto r = get_rule(node.rule->child_types[i], (size_t)which);
					node.set_child(i, makeNode(r));
				}
				which -= c;
			}
			else { // otherwise we have to process that which
				expand_to_neighbor(node.child[i], which);
			}
		}
	}

	void complete(Node& node) {
		// go through and fill in the tree at random
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child[i].is_null()) {
				node.set_child(i, generate(node.rule->child_types[i]));
			}
			else {
				complete(node.child[i]);
			}
		}
	}
	
};
