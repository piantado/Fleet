#pragma once 

#include "DeterministicLOTHypothesis.h"
#include "CachedCallHypothesis.h"

class InnerHypothesis : public DeterministicLOTHypothesis<InnerHypothesis,Utterance,TruthValue,MyGrammar,&grammar>,
					    public CachedCallHypothesis<InnerHypothesis,Utterance,TruthValue>  {
public:
	using Super = DeterministicLOTHypothesis<InnerHypothesis,Utterance,TruthValue,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	using CCH = CachedCallHypothesis<InnerHypothesis,Utterance,TruthValue>;
	
	InnerHypothesis(const InnerHypothesis& c) : Super(c), CCH(c) {}	
	InnerHypothesis(const InnerHypothesis&& c) :  Super(c), CCH(c) { }	
	
	InnerHypothesis& operator=(const InnerHypothesis& c) {
		Super::operator=(c);
		CachedCallHypothesis::operator=(c);
		return *this;
	}
	InnerHypothesis& operator=(const InnerHypothesis&& c) {
		Super::operator=(c);
		CachedCallHypothesis::operator=(c);
		return *this;
	}
	
	void set_value(Node&  v) { 
		Super::set_value(v);
		CachedCallHypothesis::clear_cache();
	}
	void set_value(Node&& v) { 
		Super::set_value(v);
		CachedCallHypothesis::clear_cache();
	}
	
	virtual double compute_prior() override {
		return prior = ( get_value().count() < MAX_NODES ? Super::compute_prior() : -infinity);
	}
	
	[[nodiscard]] virtual ProposalType propose() const override {
				
		ProposalType p; 
		
		if(flip(0.85))      p = Proposals::regenerate(&grammar, value);	
		else if(flip(0.5))  p = Proposals::sample_function_leaving_args(&grammar, value);
		else if(flip(0.5))  p = Proposals::swap_args(&grammar, value);
		else if(flip())     p = Proposals::insert_tree(&grammar, value);	
		else                p = Proposals::delete_tree(&grammar, value);			
		
		return p;
	}	
	
	
	virtual TruthValue cached_call_wrapper(const Utterance& di) override {
		// override how cached_call accesses the data
		return this->call(di, TruthValue::Undefined);
	}
	
	
};

#include "Lexicon.h"

struct ignore_t  {}; /// we don't need inputs/outputs for out MyHypothesis
class MyHypothesis : public Lexicon<MyHypothesis, std::string, InnerHypothesis, ignore_t,ignore_t, Utterance> {
	// Takes a node (in a bigger tree) and a word
	using Super = Lexicon<MyHypothesis, std::string, InnerHypothesis, ignore_t,ignore_t, Utterance>;
	using Super::Super; // inherit the constructors
public:	


	virtual double compute_likelihood(const data_t& data, double breakout=-infinity) override {
		
		// need to set this; probably not necessary?
		for(auto& [k,f] : factors) 
			f.program.loader = this; 		

		// get the cached version
		for(auto& [k,f] : factors) {
			f.cached_call(data);
			
			if(f.got_error) 
				return likelihood = -infinity;
		}

		likelihood = 0.0;
		for(size_t i=0;i<data.size();i++) {
			const Utterance& utterance = data[i]; 
			
			// Check all the words that are true and select 
			bool wtrue   = false; // was w true?
			bool wpresup = false; // was w presuppositionally valid?
			int npresup = 0; // how many satisfy the presup?
			int ntrue = 0; // how many are true AND satisfy the presup?
			int ntruepresup = 0; // how many are true and presup?
			
			// must loop over each word and see when it is true
			for(auto& [k,f] : factors) {
				// we just call the right factor on utterance -- note that the "word" in u is
				// ignored within the function call
				auto tv = f.cache[i];
				
				ntrue   += 1 * (tv == TruthValue::True);
				npresup += 1 * (tv != TruthValue::Undefined);
				ntruepresup += 1 * (tv == TruthValue::True and tv != TruthValue::Undefined);
				
				if(utterance.word == k) {
					wtrue   = (tv == TruthValue::True);
					wpresup = (tv != TruthValue::Undefined);
				}
			}

			// compute p -- equation (13) in the paper		
			
			
			/// TODO: CHECK THIS BECAUSE WHY ISNT nprseup npresuANDNOTTRUE?
			
			
			double p = (wtrue and wpresup ? alpha_p*alpha_t / ntruepresup : 0)  + 
					   (wpresup ? alpha_p*(1-alpha_t) / npresup : 0)  + 
					   (1.0-alpha_p) / words.size();				
			likelihood += log(p);
		}
		
		return likelihood;
	}

//	virtual double compute_single_likelihood(const datum_t& utterance) override {
//		
//		// need to set this; probably not necessary?
//		for(auto& [k,f] : factors) 
//			f.program.loader = this; 		
//
//		// Check all the words that are true and select 
//		bool wtrue   = false; // was w true?
//		bool wpresup = false; // was w presuppositionally valid?
//		int npresup = 0; // how many satisfy the presup?
//		int ntrue = 0; // how many are true AND satisfy the presup?
//		int ntruepresup = 0; // how many are true and presup?
//		
//		// must loop over each word and see when it is true
//		for(auto& [k,f] : factors) {
//			// we just call the right factor on utterance -- note that the "word" in u is
//			// ignored within the function call
//			auto tv = f.call(utterance, TruthValue::Undefined); 
//			
//			ntrue   += 1 * (tv == TruthValue::True);
//			npresup += 1 * (tv != TruthValue::Undefined);
//			ntruepresup += 1 * (tv == TruthValue::True and tv != TruthValue::Undefined);
//			
//			if(utterance.word == k) {
//				wtrue   = (tv == TruthValue::True);
//				wpresup = (tv != TruthValue::Undefined);
//			}
//		}
//
//		// compute p -- equation (13) in the paper		
//		double p = (wtrue and wpresup ? alpha_p*alpha_t / ntruepresup : 0)  + 
//				   (wpresup ? alpha_p*(1-alpha_t) / npresup : 0)  + 
//				   (1.0-alpha_p) / words.size();				
//
////		double p = (wtrue and wpresup ? alpha_p*alpha_t / ntruepresup : 0)  + 
////				   (wpresup ? alpha_p / npresup : 0)  + 
////				   (1.0-alpha_p) / words.size();					   
//		//print(p, wtrue, wpresup, ntrue, npresup, ntruepresup);
//		return log(p);
//	 }

};
