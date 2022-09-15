#pragma once

#include <tuple>
#include <array>
#include <exception>

#include "IO.h"
#include "Errors.h"
#include "Node.h"
#include "Random.h"
#include "Nonterminal.h"
#include "VirtualMachineState.h"
#include "VirtualMachinePool.h"
#include "Builtins.h"

// an exception for recursing too deep
struct DepthException : public std::exception {
	DepthException() {
		++FleetStatistics::depth_exceptions;
	}
};

/**
 * @class Grammar
 * @author Steven Piantadosi
 * @date 05/09/20
 * @file Grammar.h
 * @brief A grammar stores all of the rules associated with any kind of nonterminal and permits us
 * to sample as well as compute log probabilities. 
 * 
 * A grammar stores rules in a fixed (sorted) order that is guaranteed
 * not to change, and that puts terminals first and (lower priority) high probability first
 * as determined by Rule's sort order. This can be accessed with an iterator that goes over 
 * all grammar rules, via Grammar.begin() and Grammar.end()
 * 
 * The grammar hypothesis can take lambdas and parse them into rules, using GRAMMAR_TYPES.
 * 
 * The template args GRAMMAR_TYPES stores the types used in this grammar. This is inherited
 * by LOTHypothesis, which also passes them along to VirtualMachineState (in the form of
 * a Tuple). So the types used and order are fixed/standardized in the grammar
 */
template<typename _input_t, typename _output_t, typename... GRAMMAR_TYPES>
class Grammar {
public:

	using input_t = _input_t;
	using output_t = _output_t;
	using this_t = Grammar<input_t, output_t, GRAMMAR_TYPES...>;

	// Keep track of what types we are using here as our types -- thesee types are 
	// stored in this tuple so they can be extracted
	using TypeTuple = std::tuple<GRAMMAR_TYPES...>;

	// how many nonterminal types do we have?
	static constexpr size_t N_NTs = std::tuple_size<TypeTuple>::value;
	
	// The input/output types must be repeated to VirtualMachineState
	using VirtualMachineState_t = VirtualMachineState<input_t, output_t, GRAMMAR_TYPES...>;

	// This is the function type
	using FT = typename VirtualMachineState_t::FT; 
	
	// How many times will we silently ignore a DepthException
	// before tossing an assert error
	static const size_t GENERATE_DEPTH_EXCEPTION_RETRIES = 1000; 
	
	// get the n'th type
	//template<size_t N>
	//using type = typename std::tuple_element<N, TypeTuple>::type;

	// rules[k] stores a SORTED vector of rules for the kth' nonterminal. 
	// our iteration order is first for k = 0 ... N_NTs then for r in rules[k]
	std::vector<Rule>        rules[N_NTs];
	std::array<double,N_NTs> Z; // keep the normalizer handy for each nonterminal (not log space)
	
	size_t GRAMMAR_MAX_DEPTH = 64;
	
	// This function converts a type (passed as a template parameter) into a 
	// size_t index for which one it in in GRAMMAR_TYPES. 
	// This is used so that a Rule doesn't need type subclasses/templates, it can
	// store a type as e.g. nt<double>() -> size_t 
	template <class T>
	static constexpr nonterminal_t nt() {
		static_assert(sizeof...(GRAMMAR_TYPES) > 0, "*** Cannot use empty grammar types here");
		static_assert(contains_type<T, GRAMMAR_TYPES...>(), "*** The type T (decayed) must be in GRAMMAR_TYPES");
		return (nonterminal_t)TypeIndex<T, std::tuple<GRAMMAR_TYPES...>>::value;
	}
	
	Grammar() {
		for(size_t i=0;i<N_NTs;i++) {
			Z[i] = 0.0;
		}
	}
	
	// should not be doing these
	Grammar(const Grammar& g)  = delete; 
	Grammar(const Grammar&& g) = delete; 		
	
	/**
	 * @class RuleIterator
	 * @author Steven Piantadosi
	 * @date 05/09/20
	 * @file Grammar.h
	 * @brief This allows us to iterate over rules in a grammar, guaranteed to be in a fixed order (first by 
	 * 		  nonterminals, then by rule sort order. 
	 */	
	class RuleIterator : public std::iterator<std::forward_iterator_tag, Rule> {
	protected:
			this_t* grammar;
			nonterminal_t current_nt;
			std::vector<Rule>::iterator current_rule;
			
	public:
		
			RuleIterator(this_t* g, bool is_end) : grammar(g), current_nt(0) { 
				if(not is_end) {
					current_rule = g->rules[0].begin();
				} 
				else {
					// by convention we set current_rule and current_nt to the last items
					// since this is what ++ will leave them as below
					current_nt = N_NTs-1;
					current_rule = grammar->rules[current_nt].end();
				}
			}
			Rule& operator*() const  { return *current_rule; }
//			Rule* operator->() const { return  current_rule; }
			 
			RuleIterator& operator++(int blah) { this->operator++(); return *this; }
			RuleIterator& operator++() {
				
				current_rule++;
				
				// keep incrementing over rules that are empty, and if we run out of 
				// nonterminals, set us to the end and break
				while( current_rule == grammar->rules[current_nt].end() ) {
					if(current_nt < grammar->N_NTs-1) {
						current_nt++; // next nonterminal
						current_rule = grammar->rules[current_nt].begin();
					}
					else { 
						current_rule = grammar->rules[current_nt].end();
						break;
					}					
				}
				
				return *this;
			}
				
			RuleIterator& operator+(size_t n) {
				for(size_t i=0;i<n;i++) this->operator++();					
				return *this;
			}

			bool operator==(const RuleIterator& rhs) const { 
				return current_nt == rhs.current_nt and current_rule == rhs.current_rule; 
			}
	};	
	
	// these are set up to 
	RuleIterator begin() const { return RuleIterator(const_cast<this_t*>(this), false); }
	RuleIterator end()   const { return RuleIterator(const_cast<this_t*>(this), true);; }
	
	/**
	 * @brief The start nonterminal type
	 */
	constexpr nonterminal_t start() {
		return nt<output_t>();
	}
	
	constexpr size_t count_nonterminals() const {
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
		assert(nt >= 0 and nt < N_NTs);
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
	
	void change_probability(const std::string& s, const double newp) {
		Rule* r = get_rule(s);
		Z[r->nt] -= r->p;
		r->p = newp;
		Z[r->nt] += r->p;
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

	/**
	 * @brief For a given nt, returns the number of finite trees that nt can expand to if its finite; 0 if its infinite.	 * 		
	 * @param nt - the type of this nonterminal
	 */
//	void finite_size(nonterminal_t nt) const {
//		assert(nt >=0 and nt <= N_NTs);
//		
//		// need to create a 2d table of what each thing can expand to
//		std::vector<std::vector<int> > e(N_NTs, std::vector<int>(N_NTs, 0)); 
//		
//		for(auto& r : rules[nt]) {
//			for(auto& t : r.child_types)
//				++e[nt][t]; // how many ways can I get to this one?
//		}
//		
//		bool updated = false;
//		
//		do {
//			for(size_t nt=0;nt<N_NTs;nt++) {
//				for(auto& r : rules[nt]){
//					for(auto& t : r.child_types) {
//						
//					}
//				}
//			}
//			
//		} while(updated);
//	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Managing rules 
	// (this holds a lot of complexity for how we initialize from PRIMITIVES)
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


	template<typename X>
	static constexpr bool is_in_GRAMMAR_TYPES() {
		// check if X is in GRAMMAR_TYPES
		// TODO: UPDATE FOR DECAY SINCE WE DONT WANT THAT UNTIL WE HAVE REFERENCES AGAIN?
		return contains_type<X,GRAMMAR_TYPES...>();
	}
	
	/**
	 * @brief 
	 * @param fmt
	 * @param f
	 * @param p
	 * @param o
	 */		
	template<typename T, typename... args> 
	void add_vms(std::string fmt, FT* f, double p=1.0, Op o=Op::Standard, int a=0) {
		assert(f != nullptr && "*** If you're passing a null f to add_vms, you've really screwed up.");
		
		nonterminal_t Tnt = this->nt<T>();
		Rule r(Tnt, (void*)f, fmt, {nt<args>()...}, p, o, a);
		Z[Tnt] += r.p; // keep track of the total probability
		auto pos = std::lower_bound( rules[Tnt].begin(), rules[Tnt].end(), r);
		rules[Tnt].insert( pos, r ); // put this before	
	}
	
	/**
	 * @brief 
	 * @param fmt
	 * @param b
	 * @param p
	 */		
	template<typename T, typename... args> 
	void add(std::string fmt, Primitive<T,args...>& b, double p=1.0, int a=0) {
		// read f and o from b
		assert(b.f != nullptr);
		add_vms<T,args...>(fmt, (FT*)b.f, p, b.op, a);
	}
	
	/**
	 * @brief 
	 * @param fmt
	 * @param f
	 * @param p
	 * @param o
	 */	
	template<typename T, typename... args> 
	void add(std::string fmt,  std::function<T(args...)> f, double p=1.0, Op o=Op::Standard, int a=0) {
				
		// first check that the types are allowed
		static_assert((not std::is_reference<T>::value) && "*** Primitives cannot return references.");
		static_assert((not std::is_reference<args>::value && ...) && "*** Arguments cannot be references.");
		static_assert(is_in_GRAMMAR_TYPES<T>() , "*** Return type is not in GRAMMAR_TYPES");
		static_assert((is_in_GRAMMAR_TYPES<args>() && ...),	"*** Argument type is not in GRAMMAR_TYPES");
		
		// create a lambda on the heap that is a function of a VMS, since
		// this is what an instruction must be. This implements the calling order convention too. 
		//auto newf = new auto ( [=](VirtualMachineState_t* vms) -> void {
		auto fvms = new FT([=](VirtualMachineState_t* vms, int _a=0) -> void {
				assert(vms != nullptr);
			
				if constexpr (sizeof...(args) ==  0){	
					vms->push( f() );
				}
				if constexpr (sizeof...(args) ==  1) {
					auto a0 = vms->template getpop_nth<0,args...>();		
					vms->push(f(std::move(a0)));
				}
				else if constexpr (sizeof...(args) ==  2) {
					auto a1 = vms->template getpop_nth<1,args...>();	
					auto a0 = vms->template getpop_nth<0,args...>();		
					vms->push(f(std::move(a0), std::move(a1)));
				}
				else if constexpr (sizeof...(args) ==  3) {
					auto a2 = vms->template getpop_nth<2,args...>();	;
					auto a1 = vms->template getpop_nth<1,args...>();	
					auto a0 = vms->template getpop_nth<0,args...>();	
					vms->push(f(std::move(a0), std::move(a1), std::move(a2)));
				}
				else if constexpr (sizeof...(args) ==  4) {
					auto a3 = vms->template getpop_nth<3,args...>();	
					auto a2 = vms->template getpop_nth<2,args...>();	
					auto a1 = vms->template getpop_nth<1,args...>();	
					auto a0 = vms->template getpop_nth<0,args...>();		
					vms->push(f(std::move(a0), std::move(a1), std::move(a2), std::move(a3)));
				}
			});
			
		add_vms<T,args...>(fmt, fvms, p, o, a);
	}

	/**
	 * @brief 
	 * @param fmt
	 * @param p
	 * @param o
	 */
	template<typename T, typename... args> 
	void add(std::string fmt,  T(*_f)(args...), double p=1.0, Op o=Op::Standard, int a=0) {
		add<T,args...>(fmt, std::function<T(args...)>(_f), p, o, a);
	}	
	
	/**
	 * @brief Add a variable that is NOT A function -- simplification for adding alphabets etc.
	 * 		  This just wraps stuff in a thunk for us to make it easier. NOTE: x is copied by value
	 * @param fmt
	 * @param x
	 * @param p
	 * @param o
	 * @param a
	 */
	template<typename T>
	void add_terminal(std::string fmt,  T x, double p=1.0, Op o=Op::Standard, int a=0) {
		add(fmt, std::function( [=]()->T { return x; }), p, o, a);
	}	
	
	
	/**
	 * @brief Remove all the nonterminals of this type from the grammar. NOTE: This is generally a 
	 * *really* bad idea unless you know what you are doing. 
	 * @param nt
	 */	
	void remove_all(nonterminal_t nt) {
		rules[nt].clear();
		Z[nt] = 0.0;
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
		throw YouShouldNotBeHereError("*** Did not find rule in get_index_of.");
	}
	
	[[nodiscard]] virtual Rule* get_rule(const nonterminal_t nt, size_t k) const {
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
	
	[[nodiscard]] virtual Rule* get_rule(const nonterminal_t nt, const Op o, const int a=0) {
		/**
		 * @brief Get rule of type nt with a given BuiltinOp and argument a
		 * @param nt
		 * @param o
		 * @param a
		 * @return 
		 */
		 
		assert(nt >= 0 and nt < N_NTs);
		for(auto& r: rules[nt]) {
			// Need to fix this because it used is_a:
			if(r.is_a(o) and r.arg == a) 
				return &r;
		}
		throw YouShouldNotBeHereError("*** Could not find rule");		
	}
	
	[[nodiscard]] virtual Rule* get_rule(const nonterminal_t nt, size_t i) {
		return &rules[nt].at(i);
	}
	
	[[nodiscard]] virtual Rule* get_rule(const nonterminal_t nt, const std::string s) const {
		/**
		 * @brief Return a rule based on s, which must uniquely be a prefix of the rule's format of a given nonterminal type. 
		 * 			If s is the empty string, however, it must match exactly. 
		 * @param s
		 * @return 
		 */
		
		// we're going to allow matches to prefixes, but we have to keep track
		// if we have matched a prefix so we don't mutliple count (e.g if one rule was "str" and one was "string"),
		// we'd want to match "string" as "string" and not "str"
		
		bool was_partial_match = true; 
		
		Rule* ret = nullptr;
		for(auto& r: rules[nt]) {
			
			if(s == r.format) {
				if(ret != nullptr and not was_partial_match) { // if we previously found a full match
					CERR "*** Multiple rules found matching " << s TAB r.format ENDL;
					throw YouShouldNotBeHereError();
				}
				else {
					was_partial_match = false;  // not a partial match
					ret = const_cast<Rule*>(&r);
				}
			} // else we look at partial matches
			else if( was_partial_match and ((s != "" and is_prefix(s, r.format)) or (s=="" and s==r.format))) {
				if(ret != nullptr) {
					CERR "*** Multiple rules found matching " << s TAB r.format ENDL;
					throw YouShouldNotBeHereError();
				}
				else {
					ret = const_cast<Rule*>(&r);
				}
			} 
		}
		
		if(ret != nullptr) { 
			return ret; 
		}
		else {			
			CERR "*** No rule found to match " TAB QQ(s) ENDL;
			throw YouShouldNotBeHereError();
		}
	}
	
	[[nodiscard]] virtual Rule* get_rule(const std::string s) const {
		/**
		 * @brief Return a rule based on s, which must uniquely be a prefix of the rule's format.
		 * 			If s is the empty string, however, it must match exactly. 
		 * @param s
		 * @return 
		 */
		
		Rule* ret = nullptr;
		for(auto& r : *this) {
			if( (s != "" and is_prefix(s, r.format)) or (s=="" and s==r.format)) {
				if(ret != nullptr) {
					CERR "*** Multiple rules found matching " << s TAB r.format ENDL;
					assert(0);
				}
				ret = &r;
			}
		}
		
		if(ret != nullptr) { return ret; }
		else {			
			CERR "*** No rule found to match " TAB QQ(s) ENDL;
			throw YouShouldNotBeHereError();
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
		return sample<Rule,std::vector<Rule>>(rules[nt], Z[nt], f).first; // ignore the probabiltiy 
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
	

	Node __generate(const nonterminal_t ntfrom=nt<output_t>(), unsigned long depth=0) const {
		/**
		 * @brief Sample an entire tree from this grammar (keeping track of depth in case we recurse too far) of return type nt. This samples a rule, makes them with makeNode, and then recurses. 
		 * @param nt
		 * @param depth
		 * @return Returns a Node sampled from the grammar. 
		 * 
		 * NOTE: this may throw a if the grammar recurses too far (usually that means the grammar is improper)
		 */
		
		if(depth >= GRAMMAR_MAX_DEPTH) {
			#ifdef WARN_DEPTH_EXCEPTION
				CERR "*** Grammar exceeded max depth, are you sure the grammar probabilities are right?" ENDL;
				CERR "*** You might be able to figure out what's wrong with gdb and then looking at the backtrace of" ENDL;
				CERR "*** which nonterminals are called." ENDL;
				CERR "*** Or.... maybe this nonterminal does not rewrite to a terminal?" ENDL;
			#endif
			throw DepthException();
		}
		
		Rule* r = sample_rule(ntfrom);
		Node n = makeNode(r);
		
		// we'll wrap in a catch so we can see the sequence of nonterminals that failed us:
		try {
			
			for(size_t i=0;i<r->N;i++) {
				n.set_child(i, __generate(r->type(i), depth+1)); // recurse down
			}
			
		} catch(const DepthException& e) { 
			#ifdef WARN_DEPTH_EXCEPTION
				CERR ntfrom << " ";
			#endif
			throw e;
		}
		
		return n;
	}	

	/**
	 * @brief A wrapper to catch DepthExcpetions and retry. This means that defaultly we try to generate GENERATE_DEPTH_EXCEPTION_RETRIES
	 * 	      times and if ALL of them fail, we throw an assert error. Presumably, unless the grammar is terrible
	 * 		  one of them will work. This makes our DepthExceptions generally silent to the outside since
	 * 		  they won't call __generate typically 
	 * @param ntfrom
	 * @param depth
	 * @return 
	 */	
	Node generate(const nonterminal_t ntfrom=nt<output_t>(), unsigned long depth=0) const {
		for(size_t tries=0;tries<GENERATE_DEPTH_EXCEPTION_RETRIES;tries++) {
			try {
				return __generate(ntfrom, depth);
			} catch(DepthException& e) { }
		}
		assert(false && "*** Generate failed due to repeated depth exceptions");
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
			auto ret = node;
			for(size_t i=0;i<ret.nchildren();i++) {
				ret.set_child(i, copy_resample(ret.child(i), f));
			}
			return ret;
		}
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Computing log probabilities and priors
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	std::vector<size_t> get_cumulative_indices() const {
		// NOTE: This is an inefficiency because we build this up each time
		// We had a version of grammar that cached this, but it was complex and ugly
		// so I think we'll take the small performance hit and put it all in here
		const size_t NT = count_nonterminals();
		std::vector<size_t> rule_cumulative(NT); // how many rules are there before this (in our ordering)
		rule_cumulative[0] = 0;
		for(size_t nt=1;nt<NT;nt++) {
			rule_cumulative[nt] = rule_cumulative[nt-1] + count_rules( nonterminal_t(nt-1) );
		}
		return rule_cumulative;
	}
	
	std::vector<size_t> get_counts(const Node& node) const {
		/**
		 * @brief Compute a vector of counts of how often each rule was used, in a *standard* order given by iterating over nts and then iterating over rules
		 * @param node
		 * @return 
		 */
		
		const size_t R = count_rules(); 
		
		std::vector<size_t> out(R,0.0);
		
		auto rule_cumulative = get_cumulative_indices();

		for(auto& n : node) {
			// now increment out, accounting for the number of rules that must have come before!
			out[rule_cumulative[n.rule->nt] + get_index_of(n.rule)] += 1;
		}
		
		return out;
	}
	
	/**
	* @brief Compute a vector of counts of how often each rule was used, in a *standard* order given by iterating over nts and then iterating over rules
	* @param v
	* @return 
	*/
	template<typename T>
	std::vector<size_t> get_counts(const std::vector<T>& v) const {

		// NOTE: When we use requires, we can require something like T is a hypothesis with a value...
		//static_assert(std::is_base_of<LOTHypothesis,T>::value); // need to have LOTHypotheses as T to use T::get_value() below	
		
		std::vector<size_t> out(count_rules(),0.0);
		
		const auto rule_cumulative = get_cumulative_indices();		
		for(auto vi : v) {

			for(auto& n : vi.get_value()) { // assuming vi has a "value" 
				// now increment out, accounting for the number of rules that must have come before!
				out[rule_cumulative[n.rule->nt] + get_index_of(n.rule)] += 1;
			}
		}
		
		return out;
	}
	
	
	template<typename K, typename T>
	std::vector<size_t> get_counts(const std::map<K,T>& v) const {

		std::vector<size_t> out(count_rules(),0.0);
		
		const auto rule_cumulative = get_cumulative_indices();		
		for(auto vi : v) {
			for(const auto& n : vi.second.get_value()) { // assuming vi has a "value" 
				// now increment out, accounting for the number of rules that must have come before!
				out[rule_cumulative[n.rule->nt] + get_index_of(n.rule)] += 1;
			}
		}
		
		return out;
	}
	
	
	
	
//	template<typename T>
//	std::vector<size_t> get_counts(const T& v) const {
//		if constexpr (std::is_base_of<LOTHypothesis, T>) {
//			return get_counts()
//		}
//	}

	
	// If eigen is defined we can get the transition matrix	
	#ifdef AM_I_USING_EIGEN
	Matrix get_nonterminal_transition_matrix() {
		const size_t NT = count_nonterminals();
		Matrix m = Matrix::Zero(NT,NT);
		for(size_t nt=0;nt<NT;nt++) {
			double z = rule_normalizer(nt);
			for(auto& r : rules[nt]) {
				double p = r.p / z;
				for(auto& to : r.get_child_types()) {
					m(to,nt) += p;
				}
			}
		}
			
		return m;
	}
	#endif
	
	/**
	 * @brief This computes the expected length of productions from this grammar, counting terminals and nonterminals as 1 each
	 * 		   NOTE: We can do this smarter with a matrix eigenvalues, but its a bit less flexible/extensible
	 * @param max_depth
	 * @return 
	 */	
//	double get_expected_length(size_t max_depth=50) const { 
//		
//		const size_t NT = count_nonterminals();
//		nonterminal_t start = nt<output_t>();
//		// we'll build up a NT x max_depth dynamic programming table
//		
//		Vector2D<double> tab(NT, max_depth);
//		tab.fill(0.0);
//		tab[start,0] = 1; // start with 1
//		
//		for(size_t d=1;d<max_depth;d++) {
//			for(nonterminal_t nt=0;nt<NT;nt++) {
//				
//				double z = rule_normalizer(nt);
//				for(auto& r : rules[nt]) {
//					double p = r.p / z;
//					for(auto& to : r.get_child_types()) {
//						m(to,nt) += p;
//					}
//				}
//				
//					tab[d][nt] = 0.0;
//			}
//		}
//		
//		
//		double l = 0.0;
//	}
//	
	
	double log_probability(const Node& n) const {
		/**
		 * @brief Compute the log probability of a tree according to the grammar. NOTE: here we ignore nodes that are Null
		 * 		  meaning that we compute the partial probability
		 * @param n
		 * @return 
		 */
		
		double lp = 0.0;		
		for(auto& x : n) {
			if(x.rule == NullRule) continue;
			lp += log(x.rule->p) - log(rule_normalizer(x.rule->nt));
		}
	
		return lp;		
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Implementation of converting strings to nodes 
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	
	Node from_parseable(std::deque<std::string>& q) const {
		/**
		 * @brief Fills an entire tree using the string format prefixes -- see get_rule(std::string).
		 * 		  Here q should contain strings like "3:'a'" which says expand nonterminal type 3 to the rule matching 'a'
		 *        NOTE: This is not a Serializable interface because the Node needs to know which grammar
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
		
			v.set_child(i, from_parseable(q));

			if(r->type(i) != v.child(i).rule->nt) {
				CERR "*** Grammar expected type " << r->type(i) << " but got type " << v.child(i).rule->nt << " at " << r->format << " argument " << i ENDL;
				assert(false && "Bad names in from_parseable."); // just check that we didn't miss this up
			}
			
		}
		return v;
	}

	Node from_parseable(std::string s) const {
		/**
		 * @brief Expand from names where s is delimited by ':'
		 * @param s
		 * @return 
		 */
		
		std::deque<std::string> stk = split(s, Node::RuleDelimiter);    
        return from_parseable(stk);
	}
	
	Node from_parseable(const char* c) const {
		std::string s = c;
        return from_parseable(s);
	}


	size_t neighbors(const Node& node) const {
		// How many neighbors do I have? This is the number of neighbors the first gap has
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
	
	double neighbor_prior(const Node& node, int& which) const {
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
					return log(r->p)-log(rule_normalizer(r->nt));
				}
				which -= c;
			}
			else { // otherwise we have to process that which
				auto o = neighbor_prior(node.child(i), which);
				if(not std::isnan(o)) { // if this child returned something. 
					//assert(which <= 0);
					return o;
				}
			}
		}
		
		return NaN; // if no neighbors
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
	
	
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Simple parsing routines -- not very well debugged
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	std::tuple<int,std::vector<int>,int> find_open_commas_close(const std::string s) {
		
		// return values
		int openpos = -1;
		std::vector<int> commas; // all commas with only one open
		int closepos = -1; 
		
		// how many are open?
		int opencount = 0;
		
		for(size_t i=0;i<s.length();i++){
			char c = s.at(i);
			// print("C=", c, opencount, openpos, commas.size(),closepos);
			
			if(opencount==0 and openpos==-1 and c=='(') {
				openpos = i;
			}
			
			if(opencount==1 and closepos==-1 and c==')') {
				closepos = i;
			}
		
			// commas position are first comma when there is one open
			if(opencount==1 and c==','){
				assert(closepos == -1);
				commas.push_back(i);
			}
			
			opencount += (c=='(');
			opencount -= (c==')');
		}
		
		return std::make_tuple(openpos, commas, closepos);
	}

	/**
	 * @brief Very simple parsing routine that takes a string like 
	 * 			"and(not(or(eq_pos(pos(parent(x)),'NP-POSS'),eq_pos('NP-S',pos(x)))),corefers(x))"
	 * 		  (from the Binding example) and parses it into a Node. 
	 * 
	 * 		  NOTE: The resulting Node must then be put into a LOTHypothesis or Lexicon. 
	 * 	  	  NOTE: This is not very robust or debugged. Just a quick ipmlementation. 
	 * @param s
	 * @return 
	 */
	Node simple_parse(std::string s) {
		//print("Parsing ", s);
		
		// remove the lambda x. if its there
		if(s.substr(0,3) == LAMBDAXDOT_STRING) s.erase(0,3);			
		// remove leading whitespace
		while(s.at(0) == ' ' or s.at(0) == '\t') s.erase(0,1);		
		// remove trailing whitespace
		while(s.at(s.size()-1) == ' ' or s.at(s.size()-1) == '\t') s.erase(s.size()-1,1);		
		
		// use the above function to find chunks	
		auto [open, commas, close] = find_open_commas_close(s);
		
		// if it's a terminal
		if(open == -1) {
			assert(commas.size()==0 and close==-1);
			auto r = this->get_rule(s);
			return this->makeNode(r);
		}
		else if(close == open+1) { // special case of "f()"
			return this->makeNode(get_rule(s)); // the whole string is what we want and its a terminal
		}
		else { 
			assert(close != -1);
			
			// recover the rule format -- no spaces, etc. 
			std::string fmt = s.substr(0,open) + "(%s";
			for(auto& c : commas) {
				UNUSED(c);
				fmt += ",%s";
			}
			fmt += ")";
			
			// find the rule for this format
			auto r = this->get_rule(fmt);
			auto out = this->makeNode(r);
			
			int prev=open+1;
			int ci=0;
			for(auto& c : commas) {
				auto child_string = s.substr(prev,c-prev);
				out.set_child(ci,simple_parse(child_string));
				prev = c+1;
				ci++;
			}
			
			// and the last child
			auto child_string = s.substr(prev,close-prev);
			out.set_child(ci,simple_parse(child_string));
			
			return out;
		}	
	}

	
	
};
