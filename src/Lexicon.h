#ifndef LEXICON_H
#define LEXICON_H

#include <limits.h>

#include "LOTHypothesis.h"

template<typename HYP, typename T, typename t_input, typename t_output>
class Lexicon : public MCMCable<HYP,t_input,t_output>,
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

	
	Lexicon(Lexicon& l) : grammar(l.grammar), MCMCable<HYP,t_input,t_output>(l){
		for(auto v : l.factors) {
			factors.push_back(v->copy());
		}
	}
	
	Lexicon(Lexicon&& l)=delete;
	
	virtual ~Lexicon() {
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
	
	virtual size_t hash() const {
		size_t out = 0x0;
		size_t i=1;
		for(auto a: factors){
			out = hash_combine(out, hash_combine(i, a->hash()));
			i++;
		}
		return out;
	}
	
	virtual bool operator==(const Lexicon<HYP,T,t_input,t_output>& l) const {
		
		// first, fewer factors are less
		if(factors.size() != l.factors.size()) 
			return  false;
		
		for(size_t i=0;i<factors.size();i++) {
			if(!(*factors[i] == *l.factors[i]))
				return false;
		}
		return true; 
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
		
		for(auto v: factors){
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
		this->prior = log(0.5)*(factors.size()+1); // +1 to end adding facators
		for(auto a : factors) 
			this->prior += a->compute_prior();
		return this->prior;
	}
	
	
	virtual std::pair<HYP*,double> propose() const {
		HYP* x = this->copy();
		
		double fb = 0.0;
		for(size_t k=0;k<factors.size();k++) {
			if(uniform(rng) < 0.5) {
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
			auto tmp = x->factors[i];
			x->factors[i] = tmp->restart(); // restart each factor
			delete tmp;
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


#endif