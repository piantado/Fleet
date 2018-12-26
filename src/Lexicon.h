#pragma once

#include <limits.h>

#include "LOTHypothesis.h"

template<typename HYP, typename T, typename t_input, typename t_output, typename _t_datum=default_datum<t_input, t_output>>
class Lexicon : public MCMCable<HYP,t_input,t_output,_t_datum>,
				public Dispatchable<t_input,t_output>,
				public Searchable<HYP,t_input,t_output>
{
		// Store a lexicon of type T elements
	
public:
	static size_t MAX_FACTORS; // don't allow more than this many 
	std::vector<T*> factors;
	Grammar* grammar;
	
	Lexicon(Grammar* g) : grammar(g) {
		
	}
	
	Lexicon(Grammar* g, Node* v) { assert(0);}

	
	Lexicon(Lexicon& l) : grammar(l.grammar), MCMCable<HYP,t_input,t_output,_t_datum>(l){
		for(T* v : l.factors) {
			factors.push_back(v->copy());
		}
	}
	
	Lexicon(Lexicon&& l)=delete;
	
	virtual ~Lexicon() {
		for(auto v : factors)
			delete v;
		
	}
	
	void replace(size_t i, T* val) {
		// replace the i'th component here (deleting what we had there before)
		// and NOT copying val
		assert(i >= 0);
		assert(i < factors.size());
		auto tmp = factors[i];
		factors[i] = val; // restart the last factor
		delete tmp;
	}
	
	virtual std::string string() const {
		
		std::string s = "[";
		for(size_t i =0;i<factors.size();i++){
			s.append(std::string("F"));
			s.append(std::to_string(i));
			s.append(":=");
			s.append(factors[i]->string());
			s.append(".");
			if(i<factors.size()-1) s.append(" ");
		}
		s.append("]");
		return s;		
	}
	
	virtual std::string parseable(std::string delim=":", std::string fdelim="|") const {
		std::string out = "";
		for(size_t i=0;i<factors.size();i++) {
			out += factors[i]->parseable(delim) + fdelim;
		}
		return out;
	}
	
	virtual size_t hash() const {
		size_t out = 0x0;
		size_t i=1;
		for(auto a: factors){
			out = hash_combine(out, hash_combine(i, a->hash()));
			i++;
		}
		return out;
	}
	
	virtual bool operator==(const Lexicon<HYP,T,t_input,t_output,_t_datum>& l) const {
		
		// first, fewer factors are less
		if(factors.size() != l.factors.size()) 
			return  false;
		
		for(size_t i=0;i<factors.size();i++) {
			if(!(*factors[i] == *l.factors[i]))
				return false;
		}
		return true; 
	}
	
	bool has_valid_indices() const {
		// check to make sure that if we have rn recursive factors, we never try to call F on higher 
		
		// find the max op_idx used and be sure it isn't larger than the number of factors
		std::function<size_t(const Node*)> f = [](const Node* n) {
			if(n->rule->instr.is_custom && n->rule->instr.custom == op_IDX) {
				return (size_t)n->rule->instr.arg;
			}
			else {
				return (size_t)0;
			}
		};
		
		size_t mx = 0;
		for(auto a : factors) {
			auto m = a->value->maxof( f );
			if(m > mx) mx = m;
		}
		
		return mx>=0 && mx < factors.size();
	}
	 
	
	void fix_indices(size_t loc, int added) {
		// go through and fix the indices assuming that we added added at loc
		// note: Added can be positive (Adding) or negative (removing)
		std::function<void(Node*)> adjuster = [this, loc, added](Node* n){
			const Instruction ni = n->rule->instr;
			if(ni.is_custom && ni.custom == op_IDX &&
			   (size_t)ni.arg >= loc) { // we only have to increment when args is gt. loc
			   
				size_t newindex = n->rule->instr.arg+added;
				
				assert(newindex < MAX_FACTORS);
				assert(newindex >= 0);
				
				n->rule = this->grammar->get_rule(n->rule->nt, n->rule->instr.custom, newindex); 
			}
		};
			
		for(size_t i=0;i<factors.size();i++) {
			if(added > 0 || loc != i) // can't do this on the one we are removing, since we might remove f0 and it calls itself!
				factors[i]->value->map(adjuster); // replacing function mapped through nodes				
		}		
	}
	
	
	/********************************************************
	 * Required for VMS to dispatch to the right sub
	 ********************************************************/
	virtual void push_program(Program& s, short j) {
		assert(factors.size() > 0);
		
		 // or else badness -- NOTE: We could take mod or insert op_ERR, or any number of other options. 
		 // here, we decide just to defer to the subclass of this
		assert(j < (short)factors.size());

		// dispath to the right factor
		factors[j]->push_program(s); // on a LOTHypothesis, we must call wiht j=0 (j is used in Lexicon to select the right one)
	}
	
	 // This should never be called because we should be dispatching throuhg a factor
	 virtual abort_t dispatch_rule(Instruction i, VirtualMachinePool<t_input,t_output>* pool, VirtualMachineState<t_input,t_output>* vms,  Dispatchable<t_input, t_output>* loader ) {
		 assert(0 && "Should not be calling dispatch_rule on a lexicon, only on its factors");
	 }
	 
	
	/********************************************************
	 * Implementation of MCMCable interace 
	 ********************************************************/
	 
	virtual HYP* copy() const {
		auto l = new HYP(grammar);
		
		for(T* v : factors){
			l->factors.push_back(v->copy());
		}
		
		l->prior      = this->prior;
		l->likelihood = this->likelihood;
		l->posterior  = this->posterior;
		
		return l;
	}
	
	virtual HYP* copy_and_complete() const {
		
		auto l = new HYP(grammar);
		
		for(auto v: factors){
			l->factors.push_back(v->copy_and_complete());
		}
		
		return l;
	}
	
	
	virtual double compute_prior() {
		// this uses a proper prior which flips a coin to determine the number of factors
		
		this->prior = log(0.5)*(factors.size()+1); // +1 to end adding factors, as in a geometric
		//this->prior = 0.0;
		
		for(auto a : factors) 
			this->prior += a->compute_prior();
		return this->prior;
	}
	
	
	virtual std::pair<HYP*,double> propose() const {
		HYP* x = this->copy();
		
		// we will always flip one,
		// and flip the rest at random
		// if we dont always flip one, one single 
		// factors we waste half of our samples!
		//auto i = myrandom(factors.size()); 
		
		double fb = 0.0;
		for(size_t k=0;k<factors.size();k++) {
			if( flip()) {
			//if(k==i || flip()) {
				auto [h, _fb] = factors[k]->propose();
				x->replace(k, h);
				fb += _fb;
			}
		}

		return std::make_pair(x, fb);		
	}
//	
	
	virtual HYP* restart() const  {
		HYP* x = this->copy();
		for(size_t i=0;i<factors.size();i++){
			x->replace(i,x->factors[i]->restart());
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
	 int neighbors() const {
		 
		if(is_evaluable() && factors.size() < MAX_FACTORS) {
			T tmp(grammar, nullptr); // not a great way to do this -- this assumes this constructor will initialize to null (As it will for LOThypothesis)
			return tmp.neighbors();
		}
		else {
			// we should have everything complete except the last
			size_t s = factors.size();
			return factors[s-1]->neighbors();
		}
	 }
	
	 HYP* make_neighbor(int k) const {
		 
		 auto x = copy();
		 		 
		 // try adding a factor
		 if(is_evaluable() && factors.size() < MAX_FACTORS){ 
			T tmp(grammar, nullptr); // as above, assumes that this constructs will null
			assert(k < tmp.neighbors());			
			x->factors.push_back( tmp.make_neighbor(k) );	 
			return x;
		}
		else {
			// expand the last one
			size_t s = x->factors.size();
			assert(k < x->factors[s-1]->neighbors());
			x->replace(s-1, x->factors[s-1]->make_neighbor(k));
			return x;
		}
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
	 
	 
	 bool is_evaluable() const {
		for(auto a: factors) {
			if(!a->is_evaluable()) return false;
		}
		return true;
	 }
	 
	 
	/********************************************************
	 * How to call 
	 ********************************************************/
	virtual DiscreteDistribution<t_output> call(const t_input x, const t_output err) = 0; // subclass must define what it means to call a lexicon
	 
};

