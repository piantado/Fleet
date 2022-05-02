#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar>;
	using Super::Super;
	
	static constexpr double regenerate_p = 0.7;
	
	[[nodiscard]] virtual std::optional<std::pair<InnerHypothesis,double>> propose() const override {
		
		std::optional<std::pair<Node,double>> x;
		if(flip(regenerate_p))  x = Proposals::regenerate(&grammar, value);	
		else if(flip(0.1))  	x = Proposals::sample_function_leaving_args(&grammar, value);
		else if(flip(0.1))  	x = Proposals::swap_args(&grammar, value);
		else if(flip())     	x = Proposals::insert_tree(&grammar, value);	
		else                	x = Proposals::delete_tree(&grammar, value);			
		
		if(not x) { return {}; }
		
		return std::make_pair(InnerHypothesis(std::move(x.value().first)), x.value().second); 			
	}	
	
};

#include "Lexicon.h"

class MyHypothesis final : public Lexicon<MyHypothesis, int,InnerHypothesis, S, S> {
public:	
	
	unsigned long PRINT_STRINGS = 128;
	
	using Super = Lexicon<MyHypothesis, int,InnerHypothesis, S, S>;
	using Super::Super;

	virtual DiscreteDistribution<S> call(const S x=EMPTY_STRING, const S& err=S{}) {
		// this calls by calling only the last factor, which, according to our prior,
		
		// make myself the loader for all factors
		for(auto& [k,f] : factors) {
			f.program.loader = this; 
			f.was_called = false; // zero this please
		}

		return factors[factors.size()-1].call(x, err); // we call the factor but with this as the loader.  
	}
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 virtual double compute_single_likelihood(const datum_t& datum) override { assert(0); }
	 
	 // a helpful little function so other functions (like grammar inference) can get
	 // likelihoods
	 static double string_likelihood(const DiscreteDistribution<S>& M, const data_t& data, const double breakout=-infinity) {
		 
		const float log_A = log(alphabet.size());

		double ll = 0.0;
		for(const auto& a : data) {
			double alp = -infinity; // the model's probability of this
			for(const auto& m : M.values()) {				
				// we can always take away all character and generate a anew
				alp = logplusexp(alp, m.second + p_delete_append<alpha,alpha>(m.first, a.output, log_A));
			}
			ll += alp * a.count; 
			
			if(ll == -infinity) {
				return ll;
			}
			if(ll < breakout) {	
				ll = -infinity;
				break;				
			}
		}
		return ll;
	 }
	 

	 double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// this version goes through and computes the predictive probability of each prefix
		 
		const auto& M = call(EMPTY_STRING, errorstring); 
		
		// calling "call" first made was_called false on everything, and
		// this will only be set to true if it was called (via push_program)
		// so here we check and make sure everything was used and if not
		// we give it a -inf likelihood
		// NOTE: This is a slow way to do this because it requires running the hypothesis
		// but that's hard to get around with how factosr work. 
		// NOTE that this checks factors over ALL prob. outcomes
		for(auto& [k,f] : factors) {
			if(not f.was_called) {
				return likelihood=-infinity;
			}
		}
		
		// otherwise let's compute the likelihood
		return likelihood = string_likelihood(M, data, breakout); 
	
	 }
	 
	 void print(std::string prefix="") override {
		std::lock_guard guard(output_lock); // better not call Super wtih this here
		extern MyHypothesis::data_t prdata;
		extern std::string current_data;
		extern std::pair<double,double> mem_pr;
		auto o = this->call(EMPTY_STRING, errorstring);
		auto [prec, rec] = get_precision_and_recall(o, prdata, PREC_REC_N);
		COUT "#\n";
		COUT "# "; o.print(PRINT_STRINGS);	COUT "\n";
		COUT prefix << current_data TAB current_ntokens TAB mem_pr.first TAB mem_pr.second TAB 
			 this->born TAB this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->serialize()) 
			 TAB prec TAB rec TAB QQ(this->string()) ENDL
	}
	
	 
};
