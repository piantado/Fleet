#pragma once

#include <map>
#include <limits.h>

#include "Hypotheses/Interfaces/Bayesable.h"
#include "Hypotheses/Interfaces/MCMCable.h"
#include "Hypotheses/Interfaces/Searchable.h"

/**
 * @class Lexicon
 * @author piantado
 * @date 29/01/20
 * @file Lexicon.h
 * @brief A lexicon stores an association of numbers (in a vector) to some other kind of hypotheses (typically a LOTHypothesis).
 * 		  Each of these components is called a "factor." 
 */

template<typename this_t, 
		 typename key_t,
		 typename INNER, 
		 typename _input_t,
		 typename _output_t, 
		 typename datum_t=defaultdatum_t<_input_t, _output_t>,
		 typename _VirtualMachineState_t=typename INNER::Grammar_t::VirtualMachineState_t
		 >
class Lexicon : public MCMCable<this_t,datum_t>,
				public Searchable<this_t, _input_t, _output_t>, // TODO: Interface a little broken 
				public Serializable<this_t>,
				public ProgramLoader<_VirtualMachineState_t> 
{
public:
	using Grammar_t = typename INNER::Grammar_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	using VirtualMachineState_t = _VirtualMachineState_t;
	
	// Store a lexicon of type INNER elements
	const static char FactorDelimiter = '|';

	static double p_factor_propose;

	std::map<key_t,INNER> factors;
	
	Lexicon() : MCMCable<this_t,datum_t>()  { }
	
	/**
	 * @brief Return the number of factors
	 * @return 
	 */	
	size_t nfactors() const  {
		return factors.size();
	}
	
	// A lexicon's value is just that vector (this is used by GrammarHypothesis)
	      std::vector<INNER>& get_value()       {	return factors;	}
	const std::vector<INNER>& get_value() const {	return factors;	}
	
		  INNER& at(const key_t& k) { return factors.at(k); }
	const INNER& at(const key_t& k) const { return factors.at(k); }
	
		  INNER& operator[](const key_t& k) { return factors[k]; }
	const INNER& operator[](const key_t& k) const { return factors[k]; }
	
	Grammar_t* get_grammar() {
		// NOTE that since LOTHypothesis has grammar as its type, all INNER Must have the 
		// same grammar type (But this may change in the future -- if it does, we need to 
		// update GrammarHypothesis::set_hypotheses_and_data)
		return factors.begin()->get_grammar();
	}
	
	
	/**
	 * @brief Sample with n factors
	 * @param n
	 * @return 
	 */	
	[[nodiscard]] static this_t sample(std::initializer_list<key_t> lst) {
		
		this_t out;
		for(auto& k : lst){
			out[k] = INNER::sample();
		}
		
		return out;
	}
	
	virtual std::string string(std::string prefix="") const override {
		/**
		 * @brief Convert a lexicon to a string -- defaultly includes all arguments. 
		 * @return 
		 */
		
		std::string s = prefix + "[";
		for(auto& [k,f] : factors) { 
			s += "F("+str(k)+",x):=" + f.string() + ". ";
		}
		s.erase(s.size()-1); // remove last empty space
		s.append("]");
		return s;		
	}
	
	virtual size_t hash() const override {
		/**
		 * @brief Hash a Lexicon by hashing each part
		 * @return 
		 */
		
		std::hash<size_t> h;
		size_t out = h(factors.size());
		size_t i=0;
		for(auto& [k,f] : factors){
			hash_combine(out, f.hash(), k);
			i++;
		}
		return out;
	}
	
	/**
	 * @brief Equality checks equality on each part
	 * @param l
	 * @return 
	 */
	virtual bool operator==(const this_t& l) const override {
		return factors == l.factors;
	}
	
//	/**
//	 * @brief A lexicon has valid indices if calls to  op_RECURSE, op_MEM_RECURSE, op_SAFE_RECURSE, and op_SAFE_MEM_RECURSE all have arguments that are less than the size. 
//	 *  	  (So this places no restrictions on the calling earlier factors)
//	 * @return 
//	 */
//	bool has_valid_indices() const {
//		for(auto& [k, f] : factors) {
//			for(const auto& n : f.get_value() ) {
//				if(n.rule->is_recursive()) {
//					int fi = n.rule->arg; // which factor is called?
//					if(fi >= (int)factors.size() or fi < 0)
//						return false;
//				}
//			}
//		}
//		return true;
//	}
	 
//
//	bool check_reachable() const {
//		/**
//		 * @brief Check if the last factor call everything else transitively (e.g. are we "wasting" factors)
//		 * 		  We do this by making a graph of what factors call which others and then computing the transitive closure. 
//		 * 		  NOTE that this requires the key_type and assumes that a rule of that type can be gotten directly
//		 *        from the first child of a recursive call (e.g. a terminal)
//		 * @return 
//		 */
//		
//		const size_t N = factors.size();
//		assert(N > 0); 
//		
//		// is calls[i][j] stores whether factor i calls factor j
//		//bool calls[N][N]; 
//		std::vector<std::vector<bool> > calls(N, std::vector<bool>(N, false)); 
//		 
//		// everyone calls themselves, zero the rest
//		for(size_t i=0;i<N;i++) {
//			for(size_t j=0;j<N;j++){
//				calls[i][j] = (i==j);
//			}
//		}
//		
//		{
//			int i=0;
//			for(auto& [k, f] : factors) {
//				for(const auto& n : f.get_value() ) {					
//					if(n.rule->is_recursive()) {
//						
//						// NOTE This assumes that n.child[0] is directly evaluable. 
//						const Rule* r = n.child(0).rule;
//						assert(r->is_terminal()); // or else we can't use this
//						auto fptr = reinterpret_cast<VirtualMachineState_t::FT*>(r->fptr);						
//						CERR string() ENDL;
//						CERR ((*fptr)(nullptr,0)) ENDL;
//
//						BLEH this doesn't work well anymore because we have to run the function
//						
//						calls[i][n.rule->arg] = true;
//					}
//				}
//				i++;
//			}
//		}
//		
//		// now we take the transitive closure to see if calls[N-1] calls everything (eventually)
//		// otherwise it has probability of zero
//		// TOOD: This could probably be lazier because we really only need to check reachability
//		for(size_t a=0;a<N;a++) {	
//		for(size_t b=0;b<N;b++) {
//		for(size_t c=0;c<N;c++) {
//			calls[b][c] = calls[b][c] or (calls[b][a] and calls[a][c]);		
//		}
//		}
//		}
//
//		// don't do anything if we have uncalled functions from the root
//		for(size_t i=0;i<N;i++) {
//			if(not calls[N-1][i]) {
//				return false;
//			}
//		}		
//		return true;		
//	}

	
	/********************************************************
	 * Required for VMS to dispatch to the right sub
	 ********************************************************/
	 
	/**
	 * @brief Dispatch key k -- push its program onto the stack.
	 * @param s
	 * @param k
	 */	 
	virtual void push_program(Program<VirtualMachineState_t>& s, const key_t k) override {
		// dispath to the right factor
		factors.at(k).push_program(s); // on a LOTHypothesis, we must call wiht j=0 (j is used in Lexicon to select the right one)
	}
	
	/********************************************************
	 * Implementation of MCMCable interace 
	 ********************************************************/
	
	virtual void complete() override {
		for(auto& [k, f] : factors){
			f.complete();
		}
	}
		
	virtual double compute_prior() override {
		// this uses a proper prior which flips a coin to determine the number of factors
		
		this->prior = log(0.5)*(factors.size()+1); // +1 to end adding factors, as in a geometric
		
		for(auto& [k,f] : factors) {
			this->prior += f.compute_prior();
		}
		
		return this->prior;
	}

	/**
	 * @brief This proposal guarantees that there will be at least one factor that is proposed to. 
	 *        Each individual factor is proposed to with p_factor_propose
	 * @return 
	 */	
	[[nodiscard]] virtual std::pair<this_t,double> propose() const override {

		// let's first make a vector to see which factor we propose to.
		std::vector<bool> should_propose(factors.size(), false);
		for(size_t i=0;i<factors.size();i++) {
			should_propose[i] = flip(p_factor_propose);
		}
		should_propose[myrandom(should_propose.size())] = true; // always ensure one
		
		// now go through and propose to those factors
		// (NOTE fb is always zero)
		// NOTE: This is not great because it doesn't copy like we might want...
		this_t x; double fb = 0.0;
		int idx = 0;
		for(auto& [k,f] : factors) {
			if(should_propose[idx]) {
				auto [h, _fb] = f.propose();
				x.factors[k] = h;
				fb += _fb;
			} else {
				x.factors[k] = f;
			}
			idx++;
		}
		assert(x.factors.size() == factors.size());
		
		return std::make_pair(x, fb);									
	}

	
	
	[[nodiscard]] virtual this_t restart() const override {
		this_t x;
		for(auto& [k,f] : factors) {
			x.factors[k] = f.restart();
		}
		return x;
	}
	
	template<typename... A>
	[[nodiscard]] static this_t make(A... a) {
		auto h = this_t(a...);
		return h.restart();
	}
	
	/********************************************************
	 * Implementation of Searchable interace 
	 ********************************************************/
	 // Main complication is that we need to manage nullptrs/empty in order to start, so we piggyback
	 // on LOTHypothesis. To od this, we assume we can construct T with T tmp(grammar, nullptr) and 
	 // use that temporary object to compute neighbors etc. 
	 
	 // for now, we define neighbors only for *complete* (evaluable) trees -- so you can add a factor if you have a complete tree already
	 // otherwise, no adding factors
	 int neighbors() const override {
		 
		if(is_evaluable()) {
			INNER tmp;  // make something null, and then count its neighbors
			return tmp.neighbors();
		}
		else {
			// we should have everything complete except the last
			return factors.rbegin()->second.neighbors();
		}
	 }
		 
	 void expand_to_neighbor(int k) override {
		 // This is currently a bit broken -- it used to know when to add a factor
		 // but now we can't add a factor without knowing the key, so for now
		 // this is just assert(false);
		 throw NotImplementedError();
	}
	
	
	 virtual double neighbor_prior(int k) override {
		assert(k <  factors.rbegin()->second.neighbors());
		return factors.rbegin()->second.neighbor_prior(k);
	 }
	 
	 bool is_evaluable() const override {
		for(auto& [k,f]: factors) {
			if(not f.is_evaluable()) return false;
		}
		return true;
	 }
	 
	 
	/********************************************************
	 * How to call 
	 ********************************************************/
	virtual DiscreteDistribution<output_t> call(const key_t k, const input_t x, const output_t& err=output_t{}) {
		throw NotImplementedError();
	}
	 
	virtual std::string serialize() const override {
		/**
		 * @brief Convert to a parseable format (using a delimiter for each factor)
		 * @return 
		 */
		
		std::string out = str(this->prior)      + Lexicon::FactorDelimiter + 
						  str(this->likelihood) + Lexicon::FactorDelimiter +
						  str(this->posterior)  + Lexicon::FactorDelimiter;
		for(auto& [k,f] : factors) {
			out += str(k)        + Lexicon::FactorDelimiter + 
				   f.serialize() + Lexicon::FactorDelimiter;
		}
		out.erase(out.size()-1); // remove last delmiter
		return out;
	}
	
	static this_t deserialize(const std::string s) {
		/**
		 * @brief Convert a string to a lexicon of this type
		 * @param g
		 * @param s
		 * @return 
		 */

		this_t h;
		auto q = split(s, Lexicon::FactorDelimiter);
		
		h.prior      = string_to<double>(q.front()); q.pop_front();
		h.likelihood = string_to<double>(q.front()); q.pop_front();
		h.posterior  = string_to<double>(q.front()); q.pop_front();

		while(not q.empty()){
			key_t k = string_to<key_t>(q.front());  q.pop_front();
			auto v = INNER::deserialize(q.front()); q.pop_front();
			h.factors[k] = v; 
		}
		return h;
	}
};


template<typename this_t, 
		 typename key_t,
		 typename INNER, 
		 typename _input_t,
		 typename _output_t, 
		 typename datum_t,
		 typename _VirtualMachineState_t
		 >
double Lexicon<this_t,key_t,INNER,_input_t,_output_t,datum_t,_VirtualMachineState_t>::p_factor_propose = 0.5;