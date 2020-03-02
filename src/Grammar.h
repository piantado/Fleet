#pragma once

#include <tuple>
#include <deque>
#include <exception>
#include "IO.h"

#include "Node.h"
#include "Random.h"
#include "Nonterminal.h"

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
		/**
		 * @brief template function giving the index of its template argument (index in FLEET_GRAMMAR_TYPES). 
		 * NOTE: The names here are decayed (meaning that references and base types are the same. 
		 */
		using DT = typename std::decay<T>::type;
		
		static_assert(contains_type<DT, FLEET_GRAMMAR_TYPES>(), "*** The type T (decayed) must be in FLEET_GRAMMAR_TYPES");
		return TypeIndex<DT, std::tuple<FLEET_GRAMMAR_TYPES>>::value;
	}

	Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			Z[i] = 0.0;
		}
	}
	
	template<typename... T>
	Grammar(std::tuple<T...> tup) : Grammar() {
		/**
		 * @brief Constructor for grammar that uses a tuple of Primitives.
		 * @param tup - a tuple of Primitives
		 */
		
		add(tup, std::make_index_sequence<sizeof...(T)>{});
	}	
	
	Grammar(const Grammar& g) = delete; // should not be doing these
	Grammar(const Grammar&& g) = delete; // should not be doing these
	
	size_t count_nonterminals() const {
		/**
		 * @brief How many nonterminals are there in the grammar. 
		 * @return 
		 */
		
		return N_NTs;
	}
	
	size_t count_rules(const nonterminal_t nt) const {
		/**
		 * @brief Returns the number of rules of return type nt
		 * @param nt
		 * @return 
		 */		
		assert(nt < N_NTs);
		return rules[nt].size();
	}	
	size_t count_rules() const {
		/**
		 * @brief Total number of rules
		 * @return 
		 */
		
		size_t n=0;
		for(size_t i=0;i<N_NTs;i++) {
			n += count_rules((nonterminal_t)i);
		}
		return n;
	}
	size_t count_terminals(nonterminal_t nt) const {
		/**
		 * @brief Count the number of terminal rules of return type nt
		 * @param nt
		 * @return 
		 */
		
		size_t n=0;
		for(auto& r : rules[nt]) {
			if(r.is_terminal()) n++;
		}
		return n;
	}
	size_t count_nonterminals(nonterminal_t nt) const {
		/**
		 * @brief Count th enumber of non-terminal rules of return type nt
		 * @param nt
		 * @return 
		 */
		
		size_t n=0;
		for(auto& r : rules[nt]) {
			if(not r.is_terminal()) n++;
		}
		return n;
	}


	void show(std::string prefix="# ") {
		/**
		 * @brief Show the grammar by printing each rule
		 */
		
		for(nonterminal_t nt=0;nt<N_NTs;nt++) {
			for(size_t i=0;i<count_rules(nt);i++) {
				COUT prefix << get_rule(nt,i)->string() ENDL;
			}
		}
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Managing rules 
	// (this holds a lot of complexity for how we initialize from PRIMITIVES)
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	virtual void add(Rule&& r) {
		/**
		 * @brief Add a rule
		 */		
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
		(add(std::get<Is>(t)), ...); // dispatches to Primtiive or Primitives::BuiltinPrimitive
	}
	
	template<typename T, typename... args>
	void add(Primitive<T, args...> p, const int arg=0) {
		// add a single primitive -- unpacks the types to put the rule into the right place
		// NOTE: we can't use T as the return type, we have ot use p::GrammarReturnType in order to handle
		// return-by-reference primitives
		add(Rule(this->template nt<typename decltype(p)::GrammarReturnType>(), p.op, p.format, {nt<args>()...}, p.p, arg));
	}
	
	template<typename T, typename... args>
	void add(BuiltinPrimitive<T, args...> p, const int arg=0) {
		// add a single primitive -- unpacks the types to put the rule into the right place
		// NOTE: we can't use T as the return type, we have ot use p::GrammarReturnType in order to handle
		// return-by-reference primitives
		add(Rule(this->template nt<T>(), p.op, p.format, {nt<args>()...}, p.p, arg));
	}
	
	
	template<typename T, typename... args>
	void add(BuiltinOp o, std::string format, const double p=1.0, const int arg=0) {
		// add a single primitive -- unpacks the types to put the rule into the right place
		add(Rule(nt<T>(), o, format, {nt<args>()...}, p, arg));
	}
//	
	template<typename T, typename... args>
	void add(CustomOp o, std::string format, const double p=1.0, const int arg=0) {
		// add a single primitive -- unpacks the types to put the rule into the right place
		add(Rule(nt<T>(), o, format, {nt<args>()...}, p, arg));
	}


	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Methods for getting rules by some info
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	size_t get_index_of(const Rule* r) const {
		/**
		 * @brief Find the index in rules of where r is.
		 * @param r
		 * @return 
		 */
		
		for(size_t i=0;i<rules[r->nt].size();i++) {
			if(*get_rule(r->nt,i) == *r) { 
				return i;
			}
		}
		assert(false && "*** Did not find rule in get_index_of.");
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, size_t k) const {
		/**
		 * @brief Get the k'th rule of type nt 
		 * @param nt
		 * @param k
		 * @return 
		 */
		
		assert(nt < N_NTs);
		assert(k < rules[nt].size());
		return const_cast<Rule*>(&rules[nt][k]);
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const CustomOp o, const int a=0) {
		/**
		 * @brief Get rule of type nt with a given CustomOp and argument a
		 * @param nt
		 * @param o
		 * @param a
		 * @return 
		 */
		
		for(auto& r: rules[nt]) {
			if(r.instr.is_a(o) && r.instr.arg == a) 
				return &r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const BuiltinOp o, const int a=0) {
		/**
		 * @brief Get rule of type nt with a given BuiltinOp and argument a
		 * @param nt
		 * @param o
		 * @param a
		 * @return 
		 */
		for(auto& r: rules[nt]) {
			if(r.instr.is_a(o) && r.instr.arg == a) 
				return &r;
		}
		assert(0 && "*** Could not find rule");		
	}
	
//	virtual Rule* get_rule(const nonterminal_t nt, size_t i) {
//		assert(i <= rules[nt].size());
//		return &rules[nt][i];
//	}
	
	virtual Rule* get_rule(const nonterminal_t nt, const std::string s) const {
		/**
		 * @brief Return a rule based on s, which must uniquely be a prefix of the rule's format of a given nonterminal type
		 * @param s
		 * @return 
		 */
		
		Rule* ret = nullptr;
		for(auto& r: rules[nt]) {
			if(is_prefix(s, r.format)){
				if(ret != nullptr) {
					CERR "*** Multiple rules found matching " << s TAB r.format ENDL;
					assert(0);
				}
				ret = const_cast<Rule*>(&r);
			} 
		}
		
		if(ret != nullptr) { return ret; }
		else {			
			CERR "*** No rule found to match " TAB s ENDL;
			assert(0);
		}
	}
	
	virtual Rule* get_rule(const std::string s) const {
		/**
		 * @brief Return a rule based on s, which must uniquely be a prefix of the rule's format
		 * @param s
		 * @return 
		 */
		
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
		/**
		 * @brief Return the normalizing constant (NOT log) for all rules of type nt
		 * @param nt
		 * @return 
		 */
		
		assert(nt < N_NTs);
		return Z[nt];
	}

	virtual Rule* sample_rule(const nonterminal_t nt) const {
		/**
		 * @brief Randomly sample a rule of type nt. 
		 * @param nt
		 * @return 
		 */
		
		std::function<double(const Rule& r)> f = [](const Rule& r){return r.p;};
		assert(rules[nt].size() > 0 && "*** You are trying to sample from a nonterminal with no rules!");
		return sample<Rule,std::vector<Rule>>(rules[nt], f).first; // ignore the probabiltiy 
	}
	
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Generation
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
	Node makeNode(const Rule* r) const {
		/**
		 * @brief Helper function to create a node according to this grammar. This is how nodes get their log probabilities. 
		 * @param r
		 * @return 
		 */
		
		return Node(r, log(r->p)-log(rule_normalizer(r->nt)));
	}
	

	Node generate(const nonterminal_t nt, unsigned long depth=0) const {
		/**
		 * @brief Sample an entire tree from this grammar (keeping track of depth in case we recurse too far) of return type nt. This samples a rule, makes them with makeNode, and then recurses. 
		 * @param nt
		 * @param depth
		 * @return Returns a Node sampled from the grammar. 
		 * 
		 * NOTE: this may throw a DepthException if the grammar recurses too far (usually that means the grammar is improper)
		 */
		
		
		if(depth >= Fleet::GRAMMAR_MAX_DEPTH) {
			throw depth_exception; 
		}
		
		Rule* r = sample_rule(nt);
		Node n = makeNode(r);
		
		try{
			for(size_t i=0;i<r->N;i++) {
				n.set_child(i, generate(r->type(i), depth+1)); // recurse down
			}
		} catch(DepthException& e) {
			CERR "*** Grammar has recursed beyond Fleet::GRAMMAR_MAX_DEPTH (Are the probabilities right?). nt=" << nt << " d=" << depth TAB n.string() ENDL;
			throw e;
		}
		return n;
	}	
	
	template<class t>
	Node generate(unsigned long depth=0) {
		/**
		 * @brief A friendly version of generate that can be called with template by type.  
		 * @param depth
		 * @return 
		 */
		
		return generate(nt<t>(),depth);
	}
	
	Node copy_resample(const Node& node, bool f(const Node& n)) const {
		/**
		 * @brief Make a copy of node where all nodes satisfying f are regenerated from the grammar. 
		 * @param node
		 * @param f - a function saying what we should resample
		 * @return 
		 *  NOTE: this does NOT allow f to apply to nullptr children (so cannot be used to fill in)
		 */
		
		if(f(node)){
			return generate(node.rule->nt);
		}
		else {
		
			// otherwise normal copy
			Node ret = node;
			for(size_t i=0;i<ret.nchildren();i++) {
				ret.set_child(i, copy_resample(ret.child(i), f));
			}
			return ret;
		}
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Computing log probabilities and priors
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	std::vector<size_t> get_counts(const Node& node) const {
		/**
		 * @brief Compute a vector of counts of how often each rule was used, in a *standard* order given by iterating over nts and then iterating over rules
		 * @param node
		 * @return 
		 */
		
		const size_t R = count_rules(); 
		
		std::vector<size_t> out(R,0.0);
		
		// NOTE: This is an inefficiency because we build this up each time
		// We had a version of grammar that cached this, but it was complex and ugly
		// so I think we'll take the small performance hit and put it all in here
		const size_t NT = count_nonterminals();
		std::vector<size_t> rule_cumulative(NT); // how many rules are there before this (in our ordering)
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
	
	double log_probability(const Node& n) const {
		/**
		 * @brief Compute the log probability of a tree according to the grammar
		 * @param n
		 * @return 
		 */
		
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
		/**
		 * @brief Fills an entire tree using the string format prefixes -- see get_rule(std::string).
		 * 		  Here q should contain strings like "3:'a'" which says expand nonterminal type 3 to the rule matching 'a'
		 * @param q
		 * @return 
		 */
		
		assert(!q.empty() && "*** Should not ever get to here with an empty queue -- are you missing arguments?");
		
		auto [nts, pfx] = divide(q.front(), Node::NTDelimiter);
		q.pop_front();
		
		// null rules:
		if(pfx == NullRule->format) 
			return makeNode(NullRule);

		// otherwise find the matching rule
		Rule* r = this->get_rule(stoi(nts), pfx);

		Node v = makeNode(r);
		for(size_t i=0;i<r->N;i++) {	
		
			v.set_child(i, expand_from_names(q));

			if(r->type(i) != v.child(i).rule->nt) {
				CERR "*** Grammar expected type " << r->type(i) << " but got type " << v.child(i).rule->nt << " at " << r->format << " argument " << i ENDL;
				assert(false && "Bad names in expand_from_names."); // just check that we didn't miss this up
			}
			
		}
		return v;
	}

	Node expand_from_names(std::string s) const {
		/**
		 * @brief Expand from names where s is delimited by ':'
		 * @param s
		 * @return 
		 */
		
		std::deque<std::string> stk = split(s, Node::RuleDelimiter);    
        return expand_from_names(stk);
	}
	
	Node expand_from_names(const char* c) const {
		std::string s = c;
        return expand_from_names(s);
	}
	
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Enumeration -- via encoding into integers
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	

	
	Node expand_from_integer(nonterminal_t nt, IntegerizedStack& is) const {
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
		
		// NOTE: for now this doesn't work when nt only has finitely many expansions/trees
		// below it. Otherwise we'll get an assertion error eventually.
		
		enumerationidx_t numterm = count_terminals(nt);
		if(is.get_value() < numterm) {
			return makeNode(this->get_rule(nt, is.get_value()));	// whatever terminal we wanted
		}
		else {
			
			is -= numterm; // remove this from is
			auto ri =  is.pop(count_nonterminals(nt));			
			Rule* r = this->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
			Node out = makeNode(r);
			for(size_t i=0;i<r->N;i++) {
				// note that this recurses on the z format -- so it makes a new IntegerizedStack for each child
				out.set_child(i, expand_from_integer(r->type(i), i==r->N-1 ? is.get_value() : is.pop()) ) ; 
			}
			return out;					
		}

	}
	Node expand_from_integer(nonterminal_t nt, enumerationidx_t z) const {
		IntegerizedStack is(z);
		return expand_from_integer(nt, is);
	}
	
	
	enumerationidx_t compute_enumeration_order(const Node& n) {
		// inverse of the above function -- what order would we be enumerated in?
		if(n.nchildren() == 0) {
			return get_index_of(n.rule);
		}
		else {
			// here we work backwards to encode 
			nonterminal_t nt = n.rule->nt;
			enumerationidx_t numterm = count_terminals(nt);
			
			IntegerizedStack is(compute_enumeration_order(n.child(n.rule->N-1)));
			for(int i=n.rule->N-2;i>=0;i--) {
				is.push(compute_enumeration_order(n.child(i)));
			}
			is.push(get_index_of(n.rule)-numterm, count_nonterminals(nt));
			is += numterm;
			return is.get_value();
		}
	}
	

	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
		IntegerizedStack is(z);
		return lempel_ziv_full_expand(nt, is, root);
	}
		
	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
		// This implements "lempel-ziv" decoding on trees, where each integer
		// can be a reference to prior *full* subtree
#ifdef BLAHBLAH		
		// First, lowest numbers will reference prior complete subtrees
		// that are not terminals
		std::function is_full_tree = [&](const Node& ni) -> int {
			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
		};
		
		// might be a reference to a prior full tree
		int t = (root == nullptr ? 0 : root->sum(is_full_tree));
		if(is.get_value() < (size_t)t) {
			return *root->get_nth(is.get_value(), is_full_tree); 
		}
		is -= t;

		// next numbers are terminals first integers encode terminals
		enumerationidx_t numterm = count_terminals(nt);
		if(is.get_value() < numterm) {
			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
		}
		is -= numterm;

		// now we choose whether we have encoded children directly
		// or a reference to prior *subtree* 
		if(root != nullptr and is.pop(2) == 0) {
			
			// count up the number
			size_t T=0;
			for(auto& n : *root) {
				if(n.nt() == nt) {
					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
				}
			}
			
			// which subtree did I get?
			int t = is.pop(T);

			// ugh go back and find it
			Node out;
			for(auto& n : *root) {
				if(n.nt() == nt) {
					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
				}
			}
			
			
			
			
			
			
			
			
			
			
			
			auto out = copy_partial_subtree(*root, t); // get this partial tree
			
			// TODO: Make it so that a subtree we reference cannot be ONE node (so therefore
			//       the node is not null)
			
			// and now fill it in
			if(out.is_null()) {
				out = lempel_ziv_full_expand(out.nt(), is.get_value(), root);
			}
			else {
				
				std::function is_null = +[](const Node& ni) -> size_t { return 1*ni.is_null(); };
				auto b = out.sum(is_null);

				for(auto& ni : out) {
					if(ni.is_null()) {
						auto p = ni.parent; // need to set the parent to the new value
						auto pi = ni.pi;
						--b;
						// TODO: NOt right because the parent links are now broken
						p->set_child(pi, lempel_ziv_full_expand(p->rule->type(pi), (b==0 ? is.get_value() : is.pop()), root));
					}
				}
			}
			
			return out;
		}
		else { 
			// we must have encoded a normal tree expansion

			// otherwise just a direct coding
			Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
			
			// we need to fill in out's children with null for it to work
			Node out = makeNode(r);
			out.fill(); // retuired below because we'll be iterating		
			if(root == nullptr) 
				root = &out; // if not defined, out is our root
			
			for(size_t i=0;i<r->N;i++) {
				out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
			}
			return out;		
		}
#endif
		
		
		return Node(); // just to avoid warning
		
		
	}


//	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_full_expand(nt, is, root);
//	}
//		
//	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// This implements "lempel-ziv" decoding on trees, where each integer
//		// can be a reference to prior *full* subtree
//		
//		// First, lowest numbers will reference prior complete subtrees
//		// that are not terminals
//		std::function is_full_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
//		};
//		
//		std::function is_partial_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and (not ni.is_complete()) );
//		};
//			
//		int t = 0;
//		if(root != nullptr) 
//			t = root->sum(is_full_tree);
//		
//		if(is.get_value() < (size_t)t) {
//			return *root->get_nth(is.get_value(), is_full_tree); 
//		}
//		is -= t;
//		
//		// next numbers are terminals first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		is -= numterm;
//					
//		
//		// otherwise just a direct coding
//		Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // retuired below because we'll be iterating		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
//		}
//		return out;		
//		
//	}


//	Node lempel_ziv_partial_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_partial_expand(nt, is, root);
//	}
//	Node lempel_ziv_partial_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// coding where we expand to a partial tree -- this is easier because we can more easily reference prior
//		// partial trees
//		
//		// our first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		
//		is -= numterm; // we weren't any of those 
//		
//		// then the next encode some subtrees
//		if(root != nullptr) {
//			
//			auto t = count_partial_subtrees(*root);
//			if(is.get_value() < t) {
//				return copy_partial_subtree(*root, is.get_value() ); // just get the z'th tree
//			}
//			
//			is -= t;
//		}
//		
//		// now just decode		
//		auto ri = is.pop(count_nonterminals(nt)); // mod pop		
//		Rule* r = this->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // returned below because we'll be iterating		
//		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_partial_expand(r->type(i), is.get_value() == r->N-1 ? is.get_value() : is.pop(), root)); // since we are by reference, this should work right
//		}
//		return out;		
//		
//	}
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Subtrees
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	virtual enumerationidx_t count_connected_partial_subtrees(const Node& n) const {
		// how many possible connected, partial subtrees do I have
		// (e.g. including a path back to my root)
				
		assert(not n.is_null()); // can't really be connected if I'm null
	
		enumerationidx_t k=1; 
		for(enumerationidx_t i=0;i<n.nchildren();i++){
			if(not n.child(i).is_null())  {
				// for each child independently, I can either make them null, or I can include them
				// and choose one of their own subtrees
				k = k * count_connected_partial_subtrees(n.child(i)); 
			}
		}
		return k+1; // +1 because I could make the root null
	}


//	virtual Node copy_connected_partial_subtree(const Node& n, IntegerizedStack is) const {
//		// make a copy of the z'th partial subtree 
// 
//		if(z == 0) 
//			return Node(); // return null for me
//		
//		Node out = makeNode(n.rule); // copy the first level
//		for(enumerationidx_t i=0;i<out.nchildren();i++) {
//			out.set_child(i, copy_connected_partial_subtree(n.child(i), (i==out.nchildren() ? is.value() : is.pop() )));
//		}
//		return out;
//	}
//	
		
	/********************************************************
	 * Enumeration
	 ********************************************************/












	// NOTE: TEH BELOW IS WRONG BC IT DOESNT COUNT NEIGHBORS FOR LATER CHILDREN





	size_t neighbors(const Node& node) const {
		// How many neighbors do I have? We have to find every gap (nullptr child) and count the ways to expand each
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child(i).is_null()) {
				return count_rules(node.rule->type(i)); // NOTE: must use rule->child_types since child[i]->rule->nt is always 0 for NullRules
			}
			else {
				auto cn = neighbors(node.child(i));
				if(cn > 0) return cn; // we return the number of neighbors for the first gap
			}
		}
		return 0;
	}

	void expand_to_neighbor(Node& node, int& which) {
		// here we find the neighbor indicated by which and expand it into the which'th neighbor
		// to do this, we loop through until which is less than the number of neighbors,
		// and then it must specify which expansion we want to take. This means that when we
		// skip a nullptr, we have to subtract from it the number of neighbors (expansions)
		// we could have taken. 
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child(i).is_null()) {
				int c = count_rules(node.rule->type(i));
				if(which >= 0 and which < c) {
					auto r = get_rule(node.rule->type(i), (size_t)which);
					node.set_child(i, makeNode(r));
				}
				which -= c;
			}
			else { // otherwise we have to process that which
				expand_to_neighbor(node.child(i), which);
			}
		}
	}

	void complete(Node& node) {
		// go through and fill in the tree at random
		for(size_t i=0;i<node.rule->N;i++){
			if(node.child(i).is_null()) {
				node.set_child(i, generate(node.rule->type(i)));
			}
			else {
				complete(node.child(i));
			}
		}
	}
	
};
