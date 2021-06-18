#pragma once

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
		 typename INNER, 
		 typename _input_t,
		 typename _output_t, 
		 typename datum_t=defaultdatum_t<_input_t, _output_t>,
		 typename _VirtualMachineState_t=typename INNER::Grammar_t::VirtualMachineState_t
		 >
class Lexicon : public MCMCable<this_t,datum_t>,
				public Searchable<this_t, _input_t, _output_t>,
				public Callable<_input_t, _output_t, _VirtualMachineState_t>,
				public Serializable<this_t>
{
public:
	using Grammar_t = typename INNER::Grammar_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	using VirtualMachineState_t = _VirtualMachineState_t;
	
	// Store a lexicon of type INNER elements
	const static char FactorDelimiter = '|';

	std::vector<INNER> factors;
	
	Lexicon(size_t n)  : MCMCable<this_t,datum_t>()  { factors.resize(n); }
	Lexicon()          : MCMCable<this_t,datum_t>()  { }
	
	size_t nfactors() const  {
		return factors.size();
	}
	
	// A lexicon's value is just that vector (this is used by GrammarHypothesis)
	      std::vector<INNER>& get_value()       {	return factors;	}
	const std::vector<INNER>& get_value() const {	return factors;	}
	
	Grammar_t* get_grammar() {
		// NOTE that since LOTHypothesis has grammar as its type, all INNER Must have the 
		// same grammar type (But this may change in the future -- if it does, we need to 
		// update GrammarHypothesis::set_hypotheses_and_data)
		return factors[0].get_grammar();
	}
	
	
	/**
	 * @brief Sample with n factors
	 * @param n
	 * @return 
	 */	
	[[nodiscard]] static this_t sample(size_t nf) {
		this_t out(nf);
		
		for(size_t i=0;i<nf;i++) {// start with the right number of factors
			out.factors.push_back(InnerHypothesis::sample());
		}
		
		return out;
	}
	
	virtual std::string string(std::string prefix="") const override {
		/**
		 * @brief AConvert a lexicon to a string -- defaultly includes all arguments. 
		 * @return 
		 */
		
		std::string s = prefix + "[";
		for(size_t i =0;i<factors.size();i++){
			s.append(std::string("F"));
			s.append(std::to_string(i));
			s.append(":=");
			s.append(factors[i].string());
			s.append(".");
			if(i<factors.size()-1) s.append(" ");
		}
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
		for(auto& a: factors){
			hash_combine(out, a.hash(), i);
			i++;
		}
		return out;
	}
	
	virtual bool operator==(const this_t& l) const override {
		/**
		 * @brief Equality checks equality on each part
		 * @param l
		 * @return 
		 */
			
		// first, fewer factors are less
		if(factors.size() != l.factors.size()) 
			return  false;
		
		for(size_t i=0;i<factors.size();i++) {
			if(!(factors[i] == l.factors[i]))
				return false;
		}
		return true; 
	}
	
	bool has_valid_indices() const {
		/**
		 * @brief A lexicon has valid indices if calls to  op_RECURSE, op_MEM_RECURSE, op_SAFE_RECURSE, and op_SAFE_MEM_RECURSE all have arguments that are less than the size. 
		 *  	  (So this places no restrictions on the calling earlier factors)
		 * @return 
		 */
		
		// check to make sure that if we have rn recursive factors, we never try to call F on higher 
		
		for(auto& f : factors) {
			for(const auto& n : f.get_value() ) {
				if(n.rule->is_recursive()) {
					int fi = n.rule->arg; // which factor is called?
					if(fi >= (int)factors.size() or fi < 0)
						return false;
				}
			}
		}
		return true;
	}
	 

	bool check_reachable() const {
		/**
		 * @brief Check if the last factor call everything else transitively (e.g. are we "wasting" factors)
		 * 		  We do this by making a graph of what factors call which others and then computing the transitive closure. 
		 * @return 
		 */
		
		const size_t N = factors.size();
		assert(N > 0); 
		
		// is calls[i][j] stores whether factor i calls factor j
		//bool calls[N][N]; 
		std::vector<std::vector<bool> > calls(N, std::vector<bool>(N, false)); 
		 
		// everyone calls themselves, zero the rest
		for(size_t i=0;i<N;i++) {
			for(size_t j=0;j<N;j++){
				calls[i][j] = (i==j);
			}
		}
		
		for(size_t i=0;i<N;i++){
			
			for(const auto& n : factors[i].get_value() ) {
				if(n.rule->is_recursive()) {
					calls[i][n.rule->arg] = true;
				}
			}
		}
		
		// now we take the transitive closure to see if calls[N-1] calls everything (eventually)
		// otherwise it has probability of zero
		// TOOD: This could probably be lazier because we really only need to check reachability
		for(size_t a=0;a<N;a++) {	
		for(size_t b=0;b<N;b++) {
		for(size_t c=0;c<N;c++) {
			calls[b][c] = calls[b][c] or (calls[b][a] and calls[a][c]);		
		}
		}
		}

		// don't do anything if we have uncalled functions from the root
		for(size_t i=0;i<N;i++) {
			if(not calls[N-1][i]) {
				return false;
			}
		}		
		return true;		
	}

	
	/********************************************************
	 * Required for VMS to dispatch to the right sub
	 ********************************************************/
	 
//	virtual size_t program_size(short s) override {
//		return factors[s].program_size(0);
//	}
	 
	virtual void push_program(Program& s, short j) override {
		/**
		 * @brief Put factor j onto program s
		 * @param s
		 * @param j
		 */
		
		assert(factors.size() > 0);
		
		 // or else badness -- NOTE: We could take mod or insert op_ERR, or any number of other options. 
		 // here, we decide just to defer to the subclass of this
		assert(j < (short)factors.size());
		assert(j >= 0);

		// dispath to the right factor
		factors[j].push_program(s); // on a LOTHypothesis, we must call wiht j=0 (j is used in Lexicon to select the right one)
	}
	
	/********************************************************
	 * Implementation of MCMCable interace 
	 ********************************************************/
	
	virtual void complete() override {
		for(auto& v: factors){
			v.complete();
		}
	}
		
	virtual double compute_prior() override {
		// this uses a proper prior which flips a coin to determine the number of factors
		
		this->prior = log(0.5)*(factors.size()+1); // +1 to end adding factors, as in a geometric
		
		for(auto& a : factors) {
			this->prior += a.compute_prior();
		}
		
		return this->prior;
	}
	
	[[nodiscard]] virtual std::pair<this_t,double> propose() const override {
		/**
		 * @brief This proposal guarantees that there will be at least one factor that is proposed to. 
		 * 		  To do this, we draw random numbers on 2**factors.size()-1 and then use the bits of that
		 * 		  integer to determine which factors to propose to. 
		 * @return 
		 */
		
		// cannot be 0, since that is not proposing to anything
		std::uniform_int_distribution<size_t> d(1, pow(2,factors.size())-1 );
		size_t u = d(rng);
		
		// now copy over
		// TODO: Check that detailed balance is ok?
		this_t x; double fb = 0.0;
		x.factors.reserve(factors.size());
		for(size_t k=0;k<factors.size();k++) {
			if(u & 0x1) {
				auto [h, _fb] = factors[k].propose();
				x.factors.push_back(h);
				fb += _fb;
			} else {
				auto h = factors[k];
				x.factors.push_back(h);
			}
			u = u>>1;
		}

		assert(x.factors.size() == factors.size());
		return std::make_pair(x, fb);									
	}
	
	
	[[nodiscard]] virtual this_t restart() const override {
		this_t x;
		x.factors.resize(factors.size());
		for(size_t i=0;i<factors.size();i++){
			x.factors[i] = factors[i].restart();
		}
		return x;
	}
	
	template<typename... A>
	[[nodiscard]] static this_t make(A... a) {
		auto h = this_t(a...);
		return h.restart();
	}
	
	
	/********************************************************
	 * Implementation of Searchabel interace 
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
			size_t s = factors.size();
			return factors[s-1].neighbors();
		}
	 }
		 
	 void expand_to_neighbor(int k) override {
		 // try adding a factor
		 if(is_evaluable()){ 
			INNER tmp; // as above, assumes that this constructs will null
			assert(k < tmp.neighbors());			
			factors.push_back( tmp.make_neighbor(k) );	 
		}
		else {
			// expand the last factor
			size_t s = factors.size();
			assert(k < factors[s-1].neighbors());
			factors[s-1].expand_to_neighbor(k);
		}
	 }
	
	
	 virtual double neighbor_prior(int k) override {
		size_t s = factors.size();
		assert(k < factors[s-1].neighbors());
		return factors[s-1].neighbor_prior(k);
	 }
	 
	 bool is_evaluable() const override {
		for(auto& a: factors) {
			if(!a.is_evaluable()) return false;
		}
		return true;
	 }
	 
	 
	/********************************************************
	 * How to call 
	 ********************************************************/
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t& err=output_t{}, ProgramLoader* loader=nullptr) override {
		// subclass must define what it means to call a lexicon
		throw NotImplementedError();
	}
	 
	 
	 
	virtual std::string serialize() const override {
		/**
		 * @brief Convert to a parseable format (using a delimiter for each factor)
		 * @return 
		 */
		
		std::string out = "";
		for(size_t i=0;i<factors.size();i++) {
			out += factors[i].serialize();
			if(i < factors.size()-1) 
				out += Lexicon::FactorDelimiter;
		}
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
		for(auto f : split(s, Lexicon::FactorDelimiter)) {
			h.factors.push_back(INNER::deserialize(f));
		}
		return h;
	}
};

