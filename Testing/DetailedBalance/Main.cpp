#include <string>
#include <map>


// how many proposals per pair of hypotheses do we take?
size_t NSAMPLES = 1000000;

#define DO_NOT_INCLUDE_MAIN 1 
#include "../Models/FormalLanguageTheory-Simple/Main.cpp"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "TopN.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"

#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Testing");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	COUT "# Using alphabet " << alphabet ENDL;

	//------------------
	// Basic setup
	//------------------
	
	// add alphabet	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 10.0/alphabet.length());
	}
	
	MyHypothesis::regenerate_p = 0.5;
	
	// Well, this is a terrible way to test this, but golly it's easy:
	// we'll just take two random hypotheses and count up how often proposals go from one to the others
	#pragma omp parallel for
	for(size_t r=0;r<10000;r++) {
		if(CTRL_C) continue;
		auto a = MyHypothesis::sample();
		auto b = MyHypothesis::sample();
		size_t fcnt = 0;
		size_t bcnt = 0;
		double fb = NAN;
		for(size_t i=0;i<NSAMPLES and !CTRL_C;i++) {
			auto [ap, afb] = a.propose();
			auto [bp, bfb] = b.propose(); 
			if(ap == b) {
				if(std::isnan(fb)) { fb = afb; } // save this forward count
				fcnt++;
			}
			if(bp == a) bcnt++;
			
		}
		
		#pragma omp critical
		{
			if(fcnt == 0 or bcnt == 0 ) COUT "# Skipping for 0 samples:\n# "; // we will comment these out
			COUT (log(fcnt) - log(bcnt)) TAB fb TAB NSAMPLES TAB QQ(a.string()) TAB QQ(b.string()) ENDL;
		}
	}
	
	
	
}