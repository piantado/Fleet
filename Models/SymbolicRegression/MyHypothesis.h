#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "ConstantContainer.h"
#include "LOTHypothesis.h"
#include "Random.h"


// we also consider our scale variables (proposals to constants) as powers of 10
const int MIN_SCALE = -3; 
const int MAX_SCALE = 4;

// most nodes we'll consider in a hypothesis
const size_t MY_MAX_NODES = 35;


class MyHypothesis final : public ConstantContainer,
						   public LOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar> {
	
public:

	using Super = LOTHypothesis<MyHypothesis,X_t,D,MyGrammar,&grammar>;
	using Super::Super;

	virtual D callOne(const X_t x, const D err) {
		// We need to override this because LOTHypothesis::callOne asserts that the program is non-empty
		// but actually ours can be if we are only a constant. 
		// my own wrapper that zeros the constant_i counter
		
		constant_idx = 0;
		const auto out = Super::callOne(x,err);
		assert(constant_idx == constants.size()); // just check we used all constants
		return out;
	}
	
	size_t count_constants() const override {
		size_t cnt = 0;
		for(const auto& x : value) {
			cnt += isConstant(x.rule);
		}
		return cnt;
	}
	
	// Propose to a constant c, returning a new value and fb
	// NOTE: When we use a symmetric drift kernel, fb=0
	std::pair<double,double> constant_proposal(double c) const override { 
			
		if(flip(0.5)) {
			auto sc = pow(10, myrandom(MIN_SCALE, MAX_SCALE)); // pick a scale here 
			
			return std::make_pair(random_normal(c,  sc), 0.0);
		}
		else { 
			
			// one third probability for each of the other choices
			if(flip(0.33)) { 
				auto v = random_normal(data_X_mean, data_X_sd);
				double fb = normal_lpdf(v, data_X_mean, data_X_sd) - 
							normal_lpdf(c, data_X_mean, data_X_sd);
				return std::make_pair(v,fb);
			}
			else if(flip(0.5)) {
				auto v = random_normal(data_Y_mean, data_Y_sd);
				double fb = normal_lpdf(v, data_Y_mean, data_Y_sd) - 
							normal_lpdf(c, data_Y_mean, data_Y_sd);
				return std::make_pair(v,fb);
			}
			else {
				// do nothing
				return std::make_pair(c,0.0);
			}
		}
	}

	double compute_single_likelihood(const datum_t& datum) override {
		double fx = this->callOne(datum.input, NaN);
		
		if(std::isnan(fx) or std::isinf(fx)) 
			return -infinity;
			
		//PRINTN(string(), datum.output, fx, datum.reliability, normal_lpdf( fx, datum.output, datum.reliability ));
		
		return normal_lpdf(fx, datum.output, datum.reliability );		
	}
	
	
	virtual double compute_prior() override {
		
		if(this->value.count() > MY_MAX_NODES) {
			return this->prior = -infinity;
		}
		
		this->prior = Super::compute_prior() + ConstantContainer::constant_prior();
		return this->prior;
	}
	
	virtual std::string __my_string_recurse(const Node* n, size_t& idx) const {
		// we need this to print strings -- its in a similar format to evaluation
		if(isConstant(n->rule)) {
			return "("+to_string_with_precision(constants[idx++], 14)+")";
		}
		else if(n->rule->N == 0) {
			return n->rule->format;
		}
		else {
			
			// strings are evaluated in right->left order so we have to 
			// use that here (since we use them to index idx)
			std::vector<std::string> childStrings(n->nchildren());
			
			/// recurse on the children. NOTE: they are linearized left->right, 
			// which means that they are popped 
			for(size_t i=0;i<n->rule->N;i++) {
				childStrings[i] = __my_string_recurse(&n->child(i),idx);
			}
			
			std::string s = n->rule->format;
			for(size_t i=0;i<n->rule->N;i++) { // can't be size_t for counting down
				auto pos = s.find(Rule::ChildStr);
				assert(pos != std::string::npos); // must contain the ChildStr for all children all children
				s.replace(pos, Rule::ChildStr.length(), childStrings[i]);
			}
			
			return s;
		}
	}
	
	virtual std::string string(std::string prefix="") const override { 
		// we can get here where our constants have not been defined it seems...
		if(not this->is_evaluable()) 
			return structure_string(); // don't fill in constants if we aren't complete
		
		assert(constants.size() == count_constants()); // or something is broken
		
		size_t idx = 0;
		return  prefix + LAMBDAXDOT_STRING +  __my_string_recurse(&value, idx);
	}
	
	virtual std::string structure_string(bool usedot=true) const {
		return Super::string("", usedot);
	}
	
	/// *****************************************************************************
	/// Change equality to include equality of constants
	/// *****************************************************************************
	
	virtual bool operator==(const MyHypothesis& h) const override {
		// equality requires our constants to be equal 
		return this->Super::operator==(h) and ConstantContainer::operator==(h);
	}

	virtual size_t hash() const override {
		// hash includes constants so they are only ever equal if constants are equal
		size_t h = Super::hash();
		hash_combine(h, ConstantContainer::hash());
		return h;
	}
	
	/// *****************************************************************************
	/// Implement MCMC moves as changes to constants
	/// *****************************************************************************
	
	virtual ProposalType propose() const override {
		// Our proposals will either be to constants, or entirely from the prior
		// Note that if we have no constants, we will always do prior proposals
//		PRINTN("\nProposing from\t\t", string());
		
		auto NC = count_constants();
		
		if(NC > 0 and flip(0.80)){
			MyHypothesis ret = *this;
			
			double fb = 0.0; 
			
			// now add to all that I have
			for(size_t i=0;i<NC;i++) { 
				auto [v, __fb] = constant_proposal(constants[i]);
				ret.constants[i] = v;
				fb += __fb;
			}
			
			return std::make_pair(ret, fb);
		}
		else {
			
			ProposalType p; 
			
			if(flip(0.5))       p = Proposals::regenerate(&grammar, value);	
			else if(flip(0.1))  p = Proposals::sample_function_leaving_args(&grammar, value);
			else if(flip(0.1))  p = Proposals::swap_args(&grammar, value);
			else if(flip())     p = Proposals::insert_tree(&grammar, value);	
			else                p = Proposals::delete_tree(&grammar, value);			
			
			if(not p) return {};
			auto x = p.value();
			
			MyHypothesis ret{std::move(x.first)};
			ret.randomize_constants(); // with random constants -- this resizes so that it's right for propose
			
			return std::make_pair(ret, 
								  x.second + ret.constant_prior()-this->constant_prior()); 
		}
			
	}
		
	virtual MyHypothesis restart() const override {
		MyHypothesis ret = Super::restart(); // reset my structure
		ret.randomize_constants();
		return ret;
	}
	
	virtual void complete() override {
		Super::complete();
		randomize_constants();
	}
	
	virtual MyHypothesis make_neighbor(int k) const override {
		auto ret = Super::make_neighbor(k);
		ret.randomize_constants();
		return ret;
	}
	virtual void expand_to_neighbor(int k) override {
		Super::expand_to_neighbor(k);
		randomize_constants();		
	}
	
	[[nodiscard]] static MyHypothesis sample() {
		auto ret = Super::sample();
		ret.randomize_constants();
		return ret;
	}
};

