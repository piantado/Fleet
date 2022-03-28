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
	
	// set the default time
	FleetArgs::timestring = "10s";
	
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
		grammar.add_terminal( Q(S(1,c)), c, 3.0/alphabet.length());
	}
	
	
	// This is a little simulation to see what effect it has to vary the
	// terminal probability of rules. 	
	for(size_t i=0;i<1000 and !CTRL_C;i++) {
		//for(double cp=30.0;cp>3.0;cp -= 1.0) {
		for(double cp=25.0;cp>0.5 and !CTRL_C;cp -= 0.5) {
			
			// update the rule probabilities
			grammar.change_probability("\u00D8", cp);
			grammar.change_probability("x", cp);	
			for(const char c : alphabet) {
				grammar.change_probability( Q(S(1,c)), c/alphabet.length());
			}
	
			#pragma omp parallel for 
			for(size_t r=0;r<8;r++) { // need to write it this way because we can't change grammar in threads
				if(!CTRL_C) {
					TopN<MyHypothesis> best(1);
					
					auto h0 = MyHypothesis::sample();
					MCMCChain samp(h0, &mydata);
					for(auto h : samp.run(Control()) | best ) {
						UNUSED(h);
					}
					
					if(not best.empty())
						PRINTN(cp, best.best().posterior, QQ(best.best().string()));
				}
			}
		}
	}
	
}