
#define DO_NOT_INCLUDE_MAIN
#include "../FormalLanguageTheory-Simple/Main.cpp"

#include "ChainPool.h"
#include "ParallelTempering.h"
#include "Fleet.h" 
#include "MPI.h"

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.initialize(argc, argv);
	
	//------------------
	// Add the terminals to the grammar
	//------------------	
	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), S(1,c), 5.0/alphabet.length());
	}
			
	//------------------
	// set up the data
	//------------------
	
	// mydata stores the data for the inference model
	auto mydata = string_to<MyHypothesis::data_t>(datastr);
		
	/// check the alphabet
	assert(not contains(alphabet, ":"));// can't have these or else our string_to doesn't work
	assert(not contains(alphabet, ","));
	for(auto& di : mydata) {
		for(auto& c: di.output) {
			assert(contains(alphabet, c));
		}
	}
	
	//------------------
	// for MPI programs, execution splits between the head and the rest. 
	//------------------

	int provided;
	MPI_Init_thread(NULL, NULL, MPI_THREAD_FUNNELED, &provided); // need this instead of MPI_Init, since we have threads. Only the current thread will make MPI calls though
	assert(provided == MPI_THREAD_FUNNELED);
	
	if(is_mpi_head()) {
		TopN<MyHypothesis> top(TopN<MyHypothesis>::MAX_N); // take union of all
		// we will read back a bunch of tops
		for(auto& t: mpi_gather<TopN<MyHypothesis>>()) {
			top << t;
		}	
		
		top.print();
	}	
	else { // I am a worker

		BasicEnumeration<MyGrammar> be;
		
		
//		TopN<MyHypothesis> top;
//		
//		auto h0 = MyHypothesis::sample();
//		ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 10.0);
//		for(auto& h : samp.run(Control(), 100, 30000) | top) { 
//			//COUT mpi_rank() TAB h.string() ENDL;
//		}
////		
////		CERR "WORKER DONE " TAB mpi_rank() TAB top.best().string() ENDL;
//		mpi_return(top);

	}
	
	MPI_Finalize();
	
}
