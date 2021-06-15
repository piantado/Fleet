#pragma once

/**
 * @class MPIInferenceInterface
 * @author Steven Piantadosi
 * @date 15/06/21
 * @file MPIInferenceInterface.h
 * @brief 
 */
template<typename T>
class MPIInferenceInterface {
	
	virtual generator<X&> mpi_run_head() {
		TopN<MyHypothesis> top(TopN<MyHypothesis>::MAX_N); // take union of all
		// we will read back a bunch of tops
		for(auto& t: mpi_gather<TopN<MyHypothesis>>()) {
			top << t;
		}	
		for(auto& x: top) {
			co_yield x;
		}
	}
	
	virtual void mpi_run_worker(Control ctl){
		
		// defaultly we will store stuff in a top
		TopN<MyHypothesis> top;
		
		auto h0 = MyHypothesis::sample();
		
		// and run parallel tempering
		ParallelTempering samp(h0, &mydata, FleetArgs::nchains, 10.0);
		for(auto& h : samp.run(ctl, 100, 30000) | top) { }

		// CERR "WORKER DONE " TAB mpi_rank() TAB top.best().string() ENDL;
		mpi_return(top);
	}
	
	generator<X&> mpi_run(Control ctl, Args... args) {
		
		// here we split control into head, which aggregates, and the rest
		if(is_mpi_head()) {
			
		}	
		else { // I am a worker
			


		}
		
	}
};