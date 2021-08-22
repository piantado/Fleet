#include <string>
#include <map>

#define DO_NOT_INCLUDE_MAIN 1 
#include "../Models/FormalLanguageTheory-Simple/Main.cpp"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "TopN.h"
#include "MCMCChain.h"
#include "ParallelTempering.h"

#include "Fleet.h" 

using TestHypothesis = MyHypothesis;

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Testing");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	COUT "# Using alphabet " << alphabet ENDL;

	auto mydata = string_to<TestHypothesis::data_t>(datastr);
	
	//------------------
	// Basic setup
	//------------------
	
	// add alphabet	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 10.0/alphabet.length());
	}
	
	#pragma omp parallel for
	for(int reps=0;reps<1000;reps++) {
		for(double rp=0.0;rp<=1.0;rp += 0.05) {
			if(CTRL_C) continue;
			TestHypothesis::regenerate_p = rp;
			
			TopN<TestHypothesis> top;
			auto h0 = TestHypothesis::sample();
			ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 10.0);
			for(auto& h : samp.run(Control(), 250, 10000)) {
				top << h;
			}
			
			#pragma omp critical
			{
				if(not top.empty()){
					COUT rp TAB top.best().posterior TAB QQ(top.best().string()) ENDL;
				}
			}
		}
		
	}
}