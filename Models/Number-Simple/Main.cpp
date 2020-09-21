#include <vector>
#include <string>
#include <random>
#include <bitset>

// For speed, here we can just treat words as ints and use a std::bitset for sets. 
// NOTE that this is a slightly different set implementation than the original work
// (which used ints)
using word = int;
using set  = std::bitset<16>;

const word U = -999;
const double alpha = 0.9;
const double Ndata = 250; // how many data points?
double recursion_penalty = -25.0;

// probability of each set size 0,1,2,...
const std::vector<double> Np = {0.000000000, 0.683564771, 0.141145140, 0.056400989, 0.031767168, 0.028248050, 0.015693361, 0.014361803, 0.008179570, 0.009986684, 0.010652463};

/**
 * @brief Make a bit with nset number of set bits. 
 * @param nset
 * @return 
 */
set make_set(int nset) {
	set out;
	for(int i=0;i<nset;i++) out[i] = true;
	return out; 
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"

using MyGrammar = Grammar<set, word,
						  bool, word, set>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,set,word,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,set,word,MyGrammar>;
	using Super::Super;
	
	double compute_prior() override {
		// include recusion penalty
		return prior = Super::compute_prior() + (recursion_count() > 0 ? recursion_penalty : log1p(-exp(recursion_penalty))); 
	}
		
	// As in the original paper, we compute hte likelihood by averaging over all numbers, weighting by their probabilities
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		this->likelihood = 0.0;
		for(int x=1;x<10;x++) {
			auto v = callOne(make_set(x), U);
			// Likelihood is a little special here -- if we have U, then we treat it as though there
			// were no predictions (not out of 10)
			this->likelihood += Ndata*Np[x]*log( v == U ? (1.0-alpha) : (1.0-alpha)/10.0 + (v==x)*alpha);
		}
		return this->likelihood;
	}	
	
	virtual void print(std::string prefix="") override {
		std::string outputstring;
		for(int x=1;x<=10;x++) {
			auto v = callOne(make_set(x), U);
			outputstring += (v == U ? "U" : str(v)) + ".";
		}
		
		prefix += QQ(outputstring)+"\t"+std::to_string(this->recursion_count())+"\t";
		Super::print(prefix);		
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Main
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "ParallelTempering.h"
#include "Fleet.h"
#include "Top.h"

int main(int argc, char** argv) { 
	Fleet fleet("Simple number inference model");
	fleet.initialize(argc, argv);

	//------------------
	// Set up the grammar 
	//------------------	
	
	MyGrammar grammar;
	
	grammar.add("undef",         +[]() -> word { return U; });
	
	grammar.add("one",         +[]() -> word { return 1; }, 0.1);
	grammar.add("two",         +[]() -> word { return 2; }, 0.1);
	grammar.add("three",       +[]() -> word { return 3; }, 0.1);
	grammar.add("four",        +[]() -> word { return 4; }, 0.1);
	grammar.add("five",        +[]() -> word { return 5; }, 0.1);
	grammar.add("six",         +[]() -> word { return 6; }, 0.1);
	grammar.add("seven",       +[]() -> word { return 7; }, 0.1);
	grammar.add("eight",       +[]() -> word { return 8; }, 0.1);
	grammar.add("nine",        +[]() -> word { return 9; }, 0.1);
	grammar.add("ten",         +[]() -> word { return 10; }, 0.1);
	
	grammar.add("next(%s)",    +[](word w) -> word { return w == U ? U : w+1;});
	grammar.add("prev(%s)",    +[](word w) -> word { return w == U or w == 1 ? U : w-1; });
	
	// extract from the context/utterance
	grammar.add("singleton(%s)",   +[](set s) -> bool    { return s.count()==1; }, 2.0);
	grammar.add("doubleton(%s)",   +[](set s) -> bool    { return s.count()==2; }, 2.0);
	grammar.add("tripleton(%s)",   +[](set s) -> bool    { return s.count()==3; }, 2.0);
	
	grammar.add("select(%s)",      +[](set s) -> set    { 
		if(s.count()==0) {
			throw VMSRuntimeError();
		}
		else {
			for(size_t i=0;i<s.size();i++) {
				if(s[i]) { // return the first element of th eset
					set out;
					out[i] = true;
					return out;
				}
			}
			assert(false && "*** Should not get here");
		}
	});
	
	// set operations on ints -- these will modify x in place
	grammar.add("union(%s,%s)",         +[](set x, set y) -> set { return x |= y; });
	grammar.add("setdifference(%s,%s)", +[](set x, set y) -> set { return x &= ~y; });
	grammar.add("intersection(%s,%s)",  +[](set x, set y) -> set { return x &= y; });
	grammar.add("complement(%s,%s)",    +[](set x, set y) -> set { return x = ~x; }); // not in the original
	
	grammar.add("and(%s,%s)",    Builtins::And<MyGrammar>, 1./3.);
	grammar.add("or(%s,%s)",     Builtins::Or<MyGrammar>, 1./3.);
	grammar.add("not(%s)",       Builtins::Not<MyGrammar>, 1./3.);
	
	grammar.add("x",             Builtins::X<MyGrammar>, 25.0);
	grammar.add("if(%s,%s,%s)",  Builtins::If<MyGrammar,set>,  1./2);
	grammar.add("if(%s,%s,%s)",  Builtins::If<MyGrammar,word>, 1./2);
	grammar.add("recurse(%s)",   Builtins::Recurse<MyGrammar>);
		
	
	//------------------
	// Run the MCMC
	//------------------	
	
	MyHypothesis::data_t mydata; // just dummy

	// Run parallel tempering
	TopN<MyHypothesis> top;
	auto h0 = MyHypothesis::make(&grammar);
	ParallelTempering samp(h0, &mydata, top, 10, 100.0);
	samp.run(Control(), 200, 5000); 
	
	top.print();
}

