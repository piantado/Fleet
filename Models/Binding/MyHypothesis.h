#pragma once 

#include "InnerHypothesis.h"
#include "Lexicon.h"

class MyHypothesis : public Lexicon<MyHypothesis, std::string, InnerHypothesis, BindingTree*, std::string> {
	// Takes a node (in a bigger tree) and a word
	using Super = Lexicon<MyHypothesis, std::string, InnerHypothesis, BindingTree*, std::string>;
	using Super::Super; // inherit the constructors
public:	

	void clear_cache() {
		for(auto& [k,f] : factors) {
			f.clear_cache();
		}
	}

	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// TODO: Can set to null so that we get an error on recurse
		for(auto& [k, f] : factors) 
			f.program.loader = this; 
		
		// make sure everyone's cache is right on this data
		for(auto& [k, f] : factors) {
			//f.clear_cache(); // if we always want to recompute (e.g. if using recursion) -- normally we don't want this!
			f.cached_call(data); 
			
			// now if anything threw an error, break out, we don't have to compute
			if(f.got_error) {
				return likelihood = -infinity;
				
			}
		}
		
		
		// The likelihood here samples from all words that are true
		likelihood = 0.0;
		for(size_t di=0;di<data.size();di++) {
			auto& d = data[di];
			
			// see how many factors are true:
			bool wtrue = false; // was w true?
			int  ntrue = 0; // how many are true?
			
			// see which words are permitted
			for(const auto& w : words) {
				
				auto b = factors[w].cache.at(di);
				
				ntrue += 1*b;
				
				if(d.output == w) 
					wtrue = b;	
				
			
				//print(">>>", w, b);
				//if(b) {
				//	print("Word is true:", w);
				//}
			}

			// Noisy size-principle likelihood
			likelihood += (double(NDATA)/double(data.size())) * log( (wtrue ? d.reliability/ntrue : 0.0) + 
							                                         (1.0-d.reliability)/words.size());
									   
			if(likelihood < breakout) 
				return likelihood = -infinity;

		}
		
		return likelihood; 
	 }	 	

	virtual void show(std::string prefix="") override {
		
		extern MyHypothesis::data_t target_precisionrecall_data;
		extern MyHypothesis target;

		std::lock_guard guard(output_lock);
		
		COUT std::setprecision(5) << prefix << NDATA TAB this->posterior TAB this->prior TAB this->likelihood TAB "";
		
		
		// now we need to clear the caches and recompute -- NOTE this is very slow
		target.clear_cache(); target.compute_posterior(target_precisionrecall_data);
		this->clear_cache();   this->compute_posterior(target_precisionrecall_data);
		
		// when we print, we are going to compute overlap with each target item
		for(auto& w : words) {
			int nagree = 0;
			int ntot = 0;
			
			for(size_t di=0;di<target_precisionrecall_data.size();di++) {
//				auto& d = target_precisionrecall_data.at(di);
			
				if(factors[w].cache.at(di) == target.factors[w].cache.at(di)) {
					++nagree;
				}
				++ntot;
			}
			
			COUT float(nagree)/float(ntot) TAB "";
		}
		
		this->clear_cache();
		
		
		COUT QQ(this->string()) ENDL;

	}
	
	// Our restart will just choose one of the factors to restart
	[[nodiscard]] virtual MyHypothesis restart() const override {
		auto x = *this; 
		
		print(str(x));
		auto which = myrandom(factors.size());
		x[words[which]] = x[words[which]].restart();
		print(str(x));
		
		return x;
	}
	
 
};
