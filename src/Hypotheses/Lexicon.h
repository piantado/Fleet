#pragma once

#include <limits.h>

#include "LOTHypothesis.h"
#include "Hash.h"


/**
 * @class Lexicon
 * @author piantado
 * @date 29/01/20
 * @file Lexicon.h
 * @brief A lexicon stores an association of numbers (in a vector) to some other kind of hypotheses (typically a LOTHypothesis).
 * 		  Each of these components is called a "factor." 
 */

template<typename HYP, typename T, typename t_input, typename t_output, typename t_datum=default_datum<t_input, t_output>>
class Lexicon : public MCMCable<HYP,t_datum>,
				public Dispatchable<t_input,t_output>,
				public Searchable<HYP,t_input,t_output>
{
		// Store a lexicon of type T elements
	const static char FactorDelimiter = '|';
public:
	std::vector<T> factors;
	
	Lexicon(size_t n)  : MCMCable<HYP,t_datum>()  { factors.resize(n); }
	Lexicon()          : MCMCable<HYP,t_datum>()  { }
		
	virtual std::string string() const override {
		/**
		 * @brief AConvert a lexicon to a string -- defaultly includes all arguments. 
		 * @return 
		 */
		
		std::string s = "[";
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
	
	virtual std::string parseable() const {
		/**
		 * @brief Convert to a parseable format (using a delimiter for each factor)
		 * @return 
		 */
		
		std::string out = "";
		for(size_t i=0;i<factors.size();i++) {
			out += factors[i].parseable();
			if(i < factors.size()-1) 
				out += Lexicon::FactorDelimiter;
		}
		return out;
	}
	
	static HYP from_string(Grammar& g, std::string s) {
		/**
		 * @brief Convert a string to a lexicon of this type
		 * @param g
		 * @param s
		 * @return 
		 */

		HYP h;
		for(auto f : split(s, Lexicon::FactorDelimiter)) {
			T ih(&g, g.expand_from_names(f));
			h.factors.push_back(ih);
		}
		return h;
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
	
	virtual bool operator==(const HYP& l) const override {
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
		
		for(auto& a : factors) {
			for(auto& n : a.value) {
				if(n.rule->instr.is_a(BuiltinOp::op_RECURSE,BuiltinOp::op_MEM_RECURSE,BuiltinOp::op_SAFE_RECURSE,BuiltinOp::op_SAFE_MEM_RECURSE) ) {
					int fi = (size_t)n.rule->instr.arg; // which factor is called?
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
		bool calls[N][N]; 
		
		// everyone calls themselves, zero the rest
		for(size_t i=0;i<N;i++) {
			for(size_t j=0;j<N;j++){
				calls[i][j] = (i==j);
			}
		}
		
		for(size_t i=0;i<N;i++){
			
			for(const auto& n : factors[i].value) {
				if(n.rule->instr.is_a(BuiltinOp::op_RECURSE,
									  BuiltinOp::op_MEM_RECURSE,
									  BuiltinOp::op_SAFE_RECURSE,
									  BuiltinOp::op_SAFE_MEM_RECURSE)) {
					calls[i][n.rule->instr.arg] = true;
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
	
	 // This should never be called because we should be dispatching throuhg a factor
	 virtual vmstatus_t dispatch_custom(Instruction i, VirtualMachinePool<t_input,t_output>* pool, VirtualMachineState<t_input,t_output>* vms,  Dispatchable<t_input, t_output>* loader ) override {
		 assert(0); // can't call this, must be implemented by kids
	 }
	 
	
	/********************************************************
	 * Implementation of MCMCable interace 
	 ********************************************************/
	
	virtual HYP copy_and_complete() const {
		
		HYP l;
		
		for(auto& v: factors){
			l.factors.push_back(v.copy_and_complete());
		}
		
		return l;
	}
	
	
	virtual double compute_prior() override {
		// this uses a proper prior which flips a coin to determine the number of factors
		
		this->prior = log(0.5)*(factors.size()+1); // +1 to end adding factors, as in a geometric
		
		for(auto& a : factors) {
			this->prior += a.compute_prior();
		}
		
		return this->prior;
	}
	
	[[nodiscard]] virtual std::pair<HYP,double> propose() const override {
		/**
		 * @brief This proposal guarantees that there will be at least one factor that is proposed to. 
		 * 		  To do this, we draw random numbers on 2**factors.size()-1 and then use the bits of that
		 * 		  integer to determine which factors to propose to. 
		 * @return 
		 */
		
		std::uniform_int_distribution<size_t> d(1, pow(2,factors.size())-1 );
		size_t u = d(rng); // cannot be 1, since that is not proposing to anything
		
		// now copy over
		// TODO: Check that detailed balance is ok?
		HYP x; double fb = 0.0;
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
	
	
	[[nodiscard]] virtual HYP restart() const override {
		HYP x;
		x.factors.resize(factors.size());
		for(size_t i=0;i<factors.size();i++){
			x.factors[i] = factors[i].restart();
		}
		return x;
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
			T tmp;  // make something null, and then count its neighbors
			return tmp.neighbors();
		}
		else {
			// we should have everything complete except the last
			size_t s = factors.size();
			return factors[s-1].neighbors();
		}
	 }
	
	 HYP make_neighbor(int k) const override {
		 
		HYP x;
		x.factors.resize(factors.size());
		for(size_t i=0;i<factors.size();i++) {
			x.factors[i] = factors[i];
		}
		 
		 // try adding a factor
		 if(is_evaluable()){ 
			T tmp; // as above, assumes that this constructs will null
			assert(k < tmp.neighbors());			
			x.factors.push_back( tmp.make_neighbor(k) );	 
		}
		else {
			// expand the last one
			size_t s = x.factors.size();
			assert(k < x.factors[s-1].neighbors());
			x.factors[s-1] = x.factors[s-1].make_neighbor(k);
		}
		return x;
	 }
	
	 ///////////////////////////////////
	 // Old code lets you add factors any time:
//	 int neighbors() const {
//		 
//		size_t n = 0;
//		
//		// if we can add a factor
//		if(factors.size() < MAX_FACTORS) {
//			T tmp(grammar, nullptr); // not a great way to do this -- this assumes this constructor will initialize to null (As it will for LOThypothesis)
//			n += tmp.neighbors(); // this is because we can always add a factor; 
//		}
//		
//		// otherwise count the neighbors of the remaining ones
//		for(auto a: factors){
//			n += a->neighbors();
//		}
//		return n;
//	 }
	 
//	 HYP* make_neighbor(int k) const {
//		 
//		 auto x = copy();
//		 
//		 // try adding a factor
//		 if(factors.size() < MAX_FACTORS){ 
//			T tmp(grammar, nullptr); // as above, assumes that this constructs will null
//		 	if(k < tmp.neighbors()) {
//				x->factors.push_back( tmp.make_neighbor(k) );	 
//				return x;
//			}
//			k -= tmp.neighbors();
//		}
//
//		 // Now try each neighbor
//		 for(size_t i=0;i<x->factors.size();i++){
//			 int fin = x->factors[i]->neighbors();
//			 if(k < fin) { // its one of these neighbors
//				 auto t = x->factors[i];
//				 x->factors[i] = t->make_neighbor(k);
//				 delete t;				
//				 return x;
//			 }
//			 k -= fin;
//		}
//		
//		//CERR "@@@@@@" << x->neighbors() TAB k0 TAB k ENDL;
//		 
//		assert(false); // Should not get here - should have found a neighbor first
//	 }
	 
	 
	 bool is_evaluable() const override {
		for(auto& a: factors) {
			if(!a.is_evaluable()) return false;
		}
		return true;
	 }
	 
	 
	/********************************************************
	 * How to call 
	 ********************************************************/
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err) = 0; // subclass must define what it means to call a lexicon
	 
};

