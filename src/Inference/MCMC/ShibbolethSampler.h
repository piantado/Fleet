// NOTE: an interesting current feature is that when a new shibboleth is chosen, 
// it does not evaluate the current hypothesis, so that sometimes our proposals won't pass
// and those samples will go by VERY fast. 

// TOOD: This is only implemented for stochastic distributions, and there it only picks the max

// Interesting, we could do a version of this where we enforce the shibboleth only for some certain number of samples 
// and then we could esssentially have an MCMC chain that st
// 

#pragma once 

#include <utility>
#include <functional>
#include "Coroutines.h"
#include "ChainPool.h"
#include "FiniteHistory.h"
#include "Control.h"
#include "OrderedLock.h"
#include "FleetStatistics.h"
#include "Random.h"
#include "MCMCChain.h"


//#define DEBUG_MCMC 1
template<typename HYP> class ShibbolethSampler;

template<typename HYP>
class ConstrainedMCMC : public MCMCChain<HYP> {
public: 
	using input_t  = HYP::input_t;
	
	// my god we need this to figure out the return type of shibboleth_call
	using output_t =  decltype(std::declval<HYP>().shibboleth_call( std::declval<input_t>())); 
	
	input_t  sh_in;
	output_t sh_out; 
	
	// we need to store parent so that we can add to it when we get a new data type 
	ShibbolethSampler<HYP>* parent; 
	typename HYP::data_t*   data; 
	
	ConstrainedMCMC(ShibbolethSampler<HYP>* _parent, HYP& h0, input_t& in, typename HYP::data_t* d) :
		sh_in(in), parent(_parent), data(d), MCMCChain<HYP>(h0,d) {
		
		// we store the output on the shib on our initial sample
		sh_out = h0.shibboleth_call(sh_in); 
	}
	
	// this is called inside MCMCChain to quickly reject hypotheses that don't match
	virtual bool check(HYP& h) override {
		auto o = h.shibboleth_call(sh_in);
		if(o == sh_out) {
			//print("# sample ", this->current.posterior, sh_out, h.string());
			return true; 
		}
		else {
			// add if it doesn't exist already
			if(not parent->seen.contains(o)) {
				assert(parent->pool.size() + 1 < parent->MAX_POOL_SIZE);
				print("# Shibboleth adding", o, h.string());
				parent->seen.insert(o); // add this
				parent->add_chain(parent, h, sh_in, data); // note, locking done internal to ChainPool
			}
			
			// we're going to count these as samples, even though we won't return current (for speed)
			++this->samples;			
			++FleetStatistics::global_sample_count;
			
			return false; // reject this sample otherwise
		}
	}
};

template<typename HYP> 
class ShibbolethSampler : public ChainPool<HYP,ConstrainedMCMC<HYP>> {
	
public:
	using Super = ChainPool<HYP,ConstrainedMCMC<HYP>> ;
	using Super::Super;
	
	// we must pre-allocate the pool, so 
	static const size_t MAX_POOL_SIZE = 1024; 
	
	using output_t = ConstrainedMCMC<HYP>::output_t;
	
	std::set<output_t> seen; 
	
	ShibbolethSampler(HYP& h0, typename HYP::input_t& in, typename HYP::data_t* d) {		
		this->add_chain(this, h0, in, d);
		print("# starting with", in);
		
		// must reserve so they don't move/reallocate -- TODO: FIX THIS 
		this->pool.reserve(MAX_POOL_SIZE);
		this->running.reserve(MAX_POOL_SIZE);
		
	}
	
};
