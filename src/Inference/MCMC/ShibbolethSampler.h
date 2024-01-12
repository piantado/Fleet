// TODO: This spends too much time on bad hypotheses since it divides time equally
// better might be to constrain the chain. The challenge is figuring out how to update
// the constraint/chain so that it is still a correct sampler. 

// For now, we don't try to do that -- we just sample conditional on a constraint 

#pragma once 

#include <utility>
#include <functional>
#include "SampleStreams.h"
#include "ChainPool.h"
#include "FiniteHistory.h"
#include "Control.h"
#include "OrderedLock.h"
#include "FleetStatistics.h"
#include "Random.h"
#include "MCMCChain.h"

template<typename HYP>
class ConstrainedMCMC : public MCMCChain<HYP> {
public: 
	using input_t  = HYP::input_t;
	
	// my god we need this to figure out the return type of shibboleth_call
	using output_t =  decltype(std::declval<HYP>().shibboleth_call( std::declval<input_t>())); 
	
	size_t FLIP_EVERY = 10; // how many samples before we 
	bool store = false; 
	
	size_t steps = 0;
	
	input_t  sh_in;
	output_t sh_out; 
	
	ConstrainedMCMC(HYP& h0, input_t& in, typename HYP::data_t* d) :
		MCMCChain<HYP>(h0,d), sh_in(in)  {
		
		// we store the output on the shib on our initial sample
		sh_out = h0.shibboleth_call(sh_in); 
	}
	
	// this is called inside MCMCChain to quickly reject hypotheses that don't match
	virtual bool check(HYP& h) override {
		
		steps++; 
		//print(h.string());
		
		auto o = h.shibboleth_call(sh_in);
		
		if(steps % 1000 == 0) {
			::print("good", str(o));
			sh_out = o;
			store = false; // we stored it; 
			return true; 
		}
		else {
			if(o == sh_out) {
				::print("good", str(o), str(h.string()));
				return true; 
			}
			else {
				::print("bad", str(o), str(h.string()));
				++this->samples;			
				++FleetStatistics::global_sample_count;
				return false; // reject this sample otherwise but pretend it was a sample ok?
			}
		}
	
	}
	/*
	// this is called inside MCMCChain to quickly reject hypotheses that don't match
	virtual bool check(HYP& h) override {
		steps++; 
					
		// if we are in an "even" range of FLIP_EVERY we have no constraint
		// otherwise we constrain to sh_out 
		if(int(steps / FLIP_EVERY) % 2 == 0){
			store = true; 
			return true; 
		}
		else { // odd chunk of FLIP_EVERY
				
			auto o = h.shibboleth_call(sh_in);
			
			if(store) {
				sh_out = o;
				store = false; // we stored it; 
				return true; 
			}
			else {
				if(o == sh_out) {
					return true; 
				}
				else {
					++this->samples;			
					++FleetStatistics::global_sample_count;
					return false; // reject this sample otherwise but pretend it was a sample ok?
				}
			}
		}
	}*/
	
};

template<typename HYP> 
class ShibbolethSampler : public ChainPool<HYP,ConstrainedMCMC<HYP>> {
	
public:
	using Super = ChainPool<HYP,ConstrainedMCMC<HYP>> ;
	using Super::Super;
	
	ShibbolethSampler(HYP& h0, typename HYP::input_t& in, typename HYP::data_t* d) {		
		this->add_chain(h0, in, d);
	}
};


//#define DEBUG_MCMC 1
//template<typename HYP> class ShibbolethSampler;
//
//template<typename HYP>
//class ConstrainedMCMC : public MCMCChain<HYP> {
//public: 
//	using input_t  = HYP::input_t;
//	
//	// my god we need this to figure out the return type of shibboleth_call
//	using output_t =  decltype(std::declval<HYP>().shibboleth_call( std::declval<input_t>())); 
//	
//	input_t  sh_in;
//	output_t sh_out; 
//	
//	// we need to store parent so that we can add to it when we get a new data type 
//	ShibbolethSampler<HYP>* parent; 
//	typename HYP::data_t*   data; 
//	
//	ConstrainedMCMC(ShibbolethSampler<HYP>* _parent, HYP& h0, input_t& in, typename HYP::data_t* d) :
//		sh_in(in), parent(_parent), data(d), MCMCChain<HYP>(h0,d) {
//		
//		// we store the output on the shib on our initial sample
//		sh_out = h0.shibboleth_call(sh_in); 
//	}
//	
//	// this is called inside MCMCChain to quickly reject hypotheses that don't match
//	virtual bool check(HYP& h) override {
//		auto o = h.shibboleth_call(sh_in);
//		if(o == sh_out) {
//			//print("# sample ", this->current.posterior, sh_out, h.string());
//			return true; 
//		}
//		else {
//			// add if it doesn't exist already
//			if(not parent->seen.contains(o)) {
//				assert(parent->pool.size() + 1 < parent->MAX_POOL_SIZE);
//				print("# Shibboleth adding", o, h.string());
//				parent->seen.insert(o); // add this
//				parent->add_chain(parent, h, sh_in, data); // note, locking done internal to ChainPool
//			}
//			
//			// we're going to count these as samples, even though we won't return current (for speed)
//			++this->samples;			
//			++FleetStatistics::global_sample_count;
//			
//			return false; // reject this sample otherwise
//		}
//	}
//};
//
//template<typename HYP> 
//class ShibbolethSampler : public ChainPool<HYP,ConstrainedMCMC<HYP>> {
//	
//public:
//	using Super = ChainPool<HYP,ConstrainedMCMC<HYP>> ;
//	using Super::Super;
//	
//	// we must pre-allocate the pool, so 
//	static const size_t MAX_POOL_SIZE = 1024; 
//	
//	using output_t = ConstrainedMCMC<HYP>::output_t;
//	
//	std::set<output_t> seen; 
//	
//	ShibbolethSampler(HYP& h0, typename HYP::input_t& in, typename HYP::data_t* d) {		
//		this->add_chain(this, h0, in, d);
//		print("# starting with", in);
//		
//		// must reserve so they don't move/reallocate -- TODO: FIX THIS 
//		this->pool.reserve(MAX_POOL_SIZE);
//		this->running.reserve(MAX_POOL_SIZE);
//		
//	}
//	
//};
