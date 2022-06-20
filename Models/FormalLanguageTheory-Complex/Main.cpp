/* 
 * In this version, input specifies the target data minus the txt in the filename:
 * 	--input=data/NewportAslin
 * and then we'll add in the data amounts from the directory (via the vector of strings data_amounts) * 
 * */
  
#include <set>
#include <string>
#include <vector>
#include <numeric> // for gcd

#include "Data.h"

using S = std::string;
using StrSet = std::set<S>;

const std::string my_default_input = "data/English"; 

S alphabet="nvadtp";
size_t max_length = 128; // (more than 256 needed for count, a^2^n, a^n^2, etc -- see command line arg)
size_t max_setsize = 64; // throw error if we have more than this
size_t nfactors = 2; // how may factors do we run on? (defaultly)

static constexpr float alpha = 0.01; // probability of insert/delete errors (must be a float for the string function below)

size_t PREC_REC_N   = 25;  // number of top strings that we use to approximate precision and recall

const double MAX_TEMP = 1.20; 
unsigned long PRINT_STRINGS; // print at most this many strings for each hypothesis

//std::vector<S> data_amounts={"1", "2", "5", "10", "20", "50", "100", "200", "500", "1000", "2000", "5000", "10000", "50000", "100000"}; // how many data points do we run on?
std::vector<S> data_amounts={"100"}; // 

size_t current_ntokens = 0; // how many tokens are there currently? Just useful to know

const std::string errorstring = "<err>";

#include "MyGrammar.h"
#include "MyHypothesis.h"

std::string prdata_path = ""; 
MyHypothesis::data_t prdata; // used for computing precision and recall -- in case we want to use more strings?
S current_data = "";
bool long_output = false; // if true, we allow extra strings, recursions etc. on output
std::pair<double,double> mem_pr; 

////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

#ifndef DO_NOT_INCLUDE_MAIN

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "TopN.h"
#include "ParallelTempering.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// Set this
	VirtualMachineControl::MIN_LP = -15;
	FleetArgs::nchains = 5; // default number is 5
	
	FleetArgs::input_path = my_default_input; // set this so it's not fleet's normal input default
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Formal language learner");
	fleet.add_option("-N,--nfactors",      nfactors, "How many factors do we run on?");
	fleet.add_option("-L,--maxlength",     max_length, "Max allowed string length");
	fleet.add_option("-A,--alphabet",  alphabet, "The alphabet of characters to use");
	fleet.add_option("-P,--prdata",  prdata_path, "What data do we use to compute precion/recall?");
	fleet.add_option("--prN",  PREC_REC_N, "How many data points to compute precision and recall?");	
	fleet.add_flag("-l,--long-output",  long_output, "Allow extra computation/recursion/strings when we output");
	fleet.initialize(argc, argv); 

	// since we are only storing the top, we can ignore repeats from MCMC 
	FleetArgs::MCMCYieldOnlyChanges = true;
	
	
	COUT "# Using alphabet=" << alphabet ENDL;

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Set up the grammar using command line arguments
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	// each of the recursive calls we are allowed
	for(size_t i=0;i<nfactors;i++) {	
		grammar.add_terminal( str(i), (int)i, 1.0/nfactors);
	}
		
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, CONSTANT_P/alphabet.length());
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Load the data
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	// Input here is going to specify the PRdata path, minus the txt
	if(prdata_path == "") {	prdata_path = FleetArgs::input_path+".txt"; }
	
	load_data_file(prdata, prdata_path.c_str()); // put all the data in prdata
	for(auto d : prdata) {	// Add a check for any data not in the alphabet
		check_alphabet(d.output, alphabet);
	}
	
	// We are going to build up the data
	std::vector<MyHypothesis::data_t> datas; // load all the data	
	for(size_t i=0;i<data_amounts.size();i++){ 
		MyHypothesis::data_t d;
		
		S data_path = FleetArgs::input_path + "-" + data_amounts[i] + ".txt";	
		load_data_file(d, data_path.c_str());
		
		datas.push_back(d);
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Actually run
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
	// Build up an initial hypothesis with the right number of factors
	MyHypothesis h0;
	for(size_t i=0;i<nfactors;i++) { 
		h0[i] = InnerHypothesis::sample(); 
	}
 
	// where to store these hypotheses
	TopN<MyHypothesis> all; 
	
	ParallelTempering samp(h0, &datas[0], FleetArgs::nchains, MAX_TEMP); 

	// Set these up as the defaults as below
	VirtualMachineControl::MAX_STEPS  = 1024; // TODO: Change to MAX_RUN_PROGRAM or remove?
	VirtualMachineControl::MAX_OUTPUTS = 256; 
	VirtualMachineControl::MIN_LP = -15;
	PRINT_STRINGS = 512;	
	
	for(size_t di=0;di<datas.size() and !CTRL_C;di++) {
		auto& this_data = datas[di];
		
		samp.set_data(&this_data, true);
		
		// compute the prevision and recall if we just memorize the data
		{
			double cz = 0; // find the normalizer for counts			
			for(auto& d: this_data) cz += d.count; 
			
			DiscreteDistribution<S> mem_d;
			for(auto& d: this_data) 
				mem_d.addmass(d.output, log(d.count)-log(cz)); 
			
			// set a global var to be used when printing hypotheses above
			mem_pr = get_precision_and_recall(mem_d, prdata, PREC_REC_N);
		}
		
		
		// set this global variable so we know
		current_ntokens = 0;
		for(auto& d : this_data) { UNUSED(d); current_ntokens++; }
//		for(auto& h : samp.run_thread(Control(FleetArgs::steps/datas.size(), FleetArgs::runtime/datas.size(), FleetArgs::nthreads, FleetArgs::restart))) {
		for(auto& h : samp.run(Control(FleetArgs::steps/datas.size(), FleetArgs::runtime/datas.size(), FleetArgs::nthreads, FleetArgs::restart))
						| printer(FleetArgs::print) | all) {
			UNUSED(h);
		}	

		// set up to print using a larger set if we were given this option
		if(long_output){
			VirtualMachineControl::MAX_STEPS  = 32000; 
			VirtualMachineControl::MAX_OUTPUTS = 16000;
			VirtualMachineControl::MIN_LP = -40;
			PRINT_STRINGS = 5000;
		}

		all.print(data_amounts[di]);
		
		// restore
		if(long_output) {
			VirtualMachineControl::MAX_STEPS  = 1024; 
			VirtualMachineControl::MAX_OUTPUTS = 256; 
			VirtualMachineControl::MIN_LP = -15;
			PRINT_STRINGS = 512;
		}	
		
		if(di+1 < datas.size()) {
			all = all.compute_posterior(datas[di+1]); // update for next time
		}
		
	}
	
}

#endif
