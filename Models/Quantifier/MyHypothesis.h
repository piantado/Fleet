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
	
	/**
	 * @brief This computes the weight of this factor from its cached values
	 * @return 
	 */	
	double get_weight_fromcache() const {
		if(cache.size() == 0) { return 1.0; } // for empty cache
		
		int numtrue = 0;
		for(auto& v : cache) {
			numtrue += (v == TruthValue::True);
		}
		
		// use the actual weight formula
		return 1.0 / (0.1 + double(numtrue) / cache.size());
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

	void clear_cache() {
		for(auto& [k,f] : factors) {
			f.clear_cache();
		}
	}


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
		
		// first compute the weights -- these will depend on how how often each is true
		std::map<key_t,double> weights; 
		double W = 0.0; // total weight
		for(auto& [k, f] : factors) {
			weights[k] = f.get_weight_fromcache();
			W += weights[k];
		}
		

		likelihood = 0.0;
		for(size_t i=0;i<data.size();i++) {
			const Utterance& utterance = data[i]; 
			
			// Check all the words that are true and select 
			bool wtrue   = false; // was w true?
			bool wpresup = false; // was w presuppositionally valid?
			double Wpt = 0.0; // total weight of those that are true and presup 
			double Wp = 0.0; // total weight of those that are true and presup 
			
			// must loop over each word and see when it is true
			for(auto& [k,f] : factors) {
				// we just call the right factor on utterance -- note that the "word" in u is
				// ignored within the function call
				auto tv = f.cache.at(i);
				
				if(tv != TruthValue::Undefined) {
					Wp += weights[k]; // presup met
					if(tv == TruthValue::True) {
						Wpt += weights[k];
					}
				}
				
				if(utterance.word == k) {
					wtrue   = (tv == TruthValue::True);
					wpresup = (tv != TruthValue::Undefined);
				}
			}
			
			/// TODO: CHECK THIS BECAUSE WHY ISNT nprseup npresuANDNOTTRUE?
			
			// compute p -- equation (13) in the paper		
			double w = weights.at(utterance.word);
			double p = (wtrue and wpresup ? alpha_p*alpha_t*w/Wpt : 0)  + 
					   (wpresup ? alpha_p*(1-alpha_t)*w/Wp : 0)  + 
					   (1.0-alpha_p)*w/W;				
			likelihood += log(p);
		}
		
		return likelihood;
	}

	virtual std::string string(std::string prefix="") const override {
		extern MyHypothesis target; 
		
		std::string out = prefix; 
		for(auto& [k,f] : factors) {
		
			// TODO: get precision and recall relative to the target for presup and for 
			bool conservative = true; 
			int agr = 0;
			for(size_t i=0;i<PRDATA;i++) {
				auto& di = prdata[i];
				
				auto o = const_cast<InnerHypothesis*>(&f)->call(di); // we have to const_cast here since call is not const, 
				auto to = target.at(k).call(di);
				agr += (to == o); 
				
				// we compute whether we give the same answer on <A,intersection(A,B)> as <A,B>
				auto conservative_di = di; // copy the dat
				conservative_di.color = DSL::intersection(conservative_di.shape, conservative_di.color);
				auto co = const_cast<InnerHypothesis*>(&f)->call(conservative_di);
				if(co != o) { 
					conservative = false;
				}
			}

			out += str("\t", k,
							 f.get_weight_fromcache(),
							 target.at(k).get_weight_fromcache(),
							 double(agr)/PRDATA,
							 conservative,
							 QQ(f.string())) + "\n";
		}
		assert(out.size() > 0); // better not factors here 
		out.erase(out.size()-1); //remove last newline
		
		return out;
	}
	
	virtual void show(std::string prefix="") override {
		/**
		 * @brief Default printing of a hypothesis includes its posterior, prior, likelihood, and quoted string version 
		 * @param prefix
		 */	
		extern MyHypothesis target; 
		extern MyHypothesis bunny_spread; 
		extern MyHypothesis classical_spread; 

		print(":", prefix, this->posterior, this->prior, this->likelihood, target.likelihood, bunny_spread.likelihood, classical_spread.likelihood);
		print(this->string()); 
	}
};
