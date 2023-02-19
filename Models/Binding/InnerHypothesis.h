#pragma once 

#include "MyGrammar.h"
#include "DeterministicLOTHypothesis.h"
#include "Timing.h"

#include "CachedCallHypothesis.h"

class InnerHypothesis : public DeterministicLOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar>, 
						public CachedCallHypothesis<InnerHypothesis,
													defaultdatum_t<BindingTree*,std::string>, // need to use MyHypothesis' datum type
													bool> {
public:
	using Super = DeterministicLOTHypothesis<InnerHypothesis,BindingTree*,bool,MyGrammar,&grammar>;
	using Super::Super; // inherit the constructors
	using CCH = CachedCallHypothesis<InnerHypothesis,
									 defaultdatum_t<BindingTree*,std::string>,
									 bool>;
	
	const double MAX_NODES = 32; 
	
	using output_t = Super::output_t;
	using data_t = Super::data_t;
	
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
	
	virtual bool cached_call_wrapper(const defaultdatum_t<BindingTree*,std::string>& di) override {
		// override how cached_call accesses the data
		return this->call(di.input);
	}
	
	virtual double compute_prior() override {
		/* This ends up being a really important check -- otherwise we spend tons of time on really long
		 * hypotheses */
		if(this->value.count() > MAX_NODES) return this->prior = -infinity;
		else					     return this->prior = this->get_grammar()->log_probability(value);
	}
	
	[[nodiscard]] virtual std::optional<std::pair<InnerHypothesis,double>> propose() const override {
		
		std::optional<std::pair<Node,double>> p;

		if(flip(0.5))       p = Proposals::regenerate(&grammar, value);	
		else if(flip(0.1))  p = Proposals::sample_function_leaving_args(&grammar, value);
		else if(flip(0.1))  p = Proposals::swap_args(&grammar, value);
		else if(flip())     p = Proposals::insert_tree(&grammar, value);	
		else                p = Proposals::delete_tree(&grammar, value);			
		
		if(not p) return {};
		auto x = p.value();
		
		return std::make_pair(InnerHypothesis(std::move(x.first)), x.second); 
	}	

};
