#include <cmath>

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

const double reliability = 0.99;
const int N = 100; // what number do we go up to?

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define magic primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("(%s+%s)",    +[](float a, float b) -> float { return a+b; }),
	Primitive("(%s-%s)",    +[](float a, float b) -> float { return a-b; }),
	Primitive("(%s*%s)",    +[](float a, float b) -> float { return a*b; }),
	Primitive("(%s/%s)",    +[](float a, float b) -> float { return (b==0 ? 0 : a/b); }),
	Primitive("(%s^%s)",    +[](float a, float b) -> float { return pow(a,b); }),
	
	Builtin::X<float>("x")
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<float> {
	using Super=Grammar<float>;
	using Super::Super;
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define hypothesis
/// This is an example where our data is different -- it's a set instead of a vector
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include<set>
#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,float,float,MyGrammar,float,std::multiset<float> > {
public:
	using Super = LOTHypothesis<MyHypothesis,float,float,MyGrammar,float,std::multiset<float>>;
	using Super::Super; // inherit the constructors
	
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// TODO: This uses 0 for NAN -- probably not the best...
		
		std::set<float> s;
		float fx;
		for(int i=0;i<=N and !CTRL_C;i++) {
			fx = this->callOne(i, 0, this);
			
			if(fx >=0 and fx <= N and !std::isnan(fx)) { // important to check bounds for size principle
				s.insert(fx);
			}
		}
		
		double sz = s.size(); // size principle likelihood
		
		// now go through and compute the likelihood
		likelihood = 0.0;
		for(auto& d : data) {
			bool b = (s.find(d)!=s.end()); // does it contain?
			likelihood += log(  (b ? reliability / sz : 0.0) + (1.0-reliability)/N);
		}
		
		return likelihood;
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////

#include "Fleet.h"
#include "Top.h"
#include "MCMCChain.h"

int main(int argc, char** argv){ 
	auto app = Fleet::DefaultArguments("Number game");
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize();

	MyGrammar grammar(PRIMITIVES);
	
	for(int i=0;i<=N;i++) {
		grammar.add<float>(BuiltinOp::op_FLOAT, str(i), 10.0/N, i);		
	}


	// Define something to hold the best hypotheses
	TopN<MyHypothesis> top(ntop);

	// our data
	MyHypothesis::data_t mydata = {2,8,16};	

	// create a hypothesis
	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	
	// and sample with just one chain
	MCMCChain samp(h0, &mydata, top);
	tic();
	samp.run(Control(mcmc_steps, runtime)); //30000);		
	tic();

	// print the results
	top.print();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;
}
