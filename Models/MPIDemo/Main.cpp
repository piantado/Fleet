
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

	//MPI_Init(NULL, NULL);
	int provided;
	MPI_Init_thread(NULL, NULL, MPI_THREAD_FUNNELED, &provided); // need this instead of MPI_Init, since we have threads. Only the current thread will make MPI calls though
	assert(provided == MPI_THREAD_FUNNELED);
	
	if(is_mpi_head()) {
		TopN<MyHypothesis> top(99999); // take union of all
		// we will read back a bunch of tops
		for(auto& t: mpi_gather<TopN<MyHypothesis>>()) {
			top << t;
			t.print();
		}	
		
		top.print();
	}	
	else { // I am a worker
		
		//rng.seed(mpi_rank());
//		COUT rng << "" ENDL;
//		  std::array<int,624> seed_data;
//		  std::random_device r;
//		  std::generate_n(seed_data.data(), seed_data.size(), std::ref(r));
//		  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
//		  rng.seed(seq);
		
		TopN<MyHypothesis> top;
		
//		for(int i=0;i<10;i++){
//			auto h0 = MyHypothesis::sample();
//			CERR mpi_rank() TAB h0.string() ENDL;
//		}
		
		auto h0 = MyHypothesis::sample();
//		ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 10.0);
//		for(auto& h : samp.run(Control(), 100, 30000) | top) { 
//		MCMCChain samp(h0, &mydata);
		ChainPool samp(h0, &mydata, FleetArgs::nchains);
		for(auto& h : samp.run(Control()) | top) { 
			COUT mpi_rank() TAB h.string() ENDL;
		}
//		//h0.compute_posterior(mydata);
//		//top << h0;
//		
//		CERR "WORKER DONE " TAB mpi_rank() TAB top.best().string() ENDL;
		mpi_return(top);

	}
	
	MPI_Finalize();
	
}
