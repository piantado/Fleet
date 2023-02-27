//#define DEBUG_MCMC 1

// we have a "feynman" flag -- when true, we use the feynman grammar, only search for the best, etc. 
//#define FEYNMAN 0

// if we want to require that hypotheses use all variables
// here we will defaultly require that for FEYNMAN 
//#if FEYNMAN
//	#define REQUIRE_USE_ALL_VARIABLES 1
//#endif 

#include <cmath>
#include "Numerics.h"

using D = double;

const double sdscale  = 1.0; // can change if we want
int    polynomial_degree = -1; //-1 means do everything, otherwise store ONLY polynomials less than or equal to this bound
double end_at_likelihood = infinity; // if we get this value in log likelihood, we can stop everything (for Feynman)

size_t BURN_N = 0; // 1000; // burn this many at the start of each MCMC chain -- probably NOT needed if doing weighted samples

size_t nsamples = 100; // Need lots of samples per structure in order to get reliable IQR for e.g. means
size_t nstructs = 100; //100 // print out all the samples from the top this many structures

size_t head_data = 0; // if nonzero, this is the max number of lines we'll read in

double FEYNMAN_SD = 0.1; //0.01; // when we run feynman, use this SD (times the data Y SD)

// these are used for some constant proposals, and so must be defined before
// hypotheses. They ar efilled in when we read the data
double data_X_mean = NaN;
double data_Y_mean = NaN;
double data_X_sd   = NaN;
double data_Y_sd   = NaN;
double best_possible_ll = NaN; // what is the best ll we could have gotten?
char sep = '\t'; // default input separator

// Define a type for our arguments (which is essentially a tuple/array that prints nice)
#include "Strings.h"

const size_t MAX_VARS = 9; // arguments are at most this many 
size_t       NUM_VARS = 1; // how many predictor variables 

// What time do we store our argument as? We use this
// format because it allows multiple variables
using X_t = std::array<D,MAX_VARS>;

// Friendly printing of variable names but only the first NUM_VARS of them
std::ostream& operator<<(std::ostream& o, X_t a) {
	
	std::string out = "";
	
	for(size_t i=0;i<NUM_VARS;i++) {
		out = out + str(a[i]) + " ";
	}
	out.erase(out.size()-1); // last separator
	
	o << "<"+out+">";
	
	return o;
}

#include "MyGrammar.h"
#include "MyHypothesis.h"
#include "MyMCTS.h"
#include "StructureToSamples.h"

MyHypothesis::data_t mydata;

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "PartitionMCMC.h"
#include "FleetArgs.h"
#include "Fleet.h" 

#ifndef DO_NOT_INCLUDE_MAIN

int main(int argc, char** argv){ 
	
	// Just a note here -- if we are storing samples via a weighted sampler, then we don't
	// want multiple duplicates of the same sample. If we are using an unweighted version, then we do.
	FleetArgs::MCMCYieldOnlyChanges = false;
	
	double maxT = 10.0; // max temperature we see
	bool pt_test_output = false; // output what we want for the PT testing
		
	// cannot have a likelhood breakout because some likelihoods are positive
	FleetArgs::LIKELIHOOD_BREAKOUT = false; 
	
	// we use parallel tempering for real samples, so we only want temp=1
	FleetArgs::yieldOnlyChainOne = true; 
	
	// how we separate times
	std::string strsep = "\t";
	
	FleetArgs::inner_timestring = "1m"; // default inner time
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Symbolic regression");
	fleet.add_option("--nvars", NUM_VARS, "How many predictor variables are there?");
	fleet.add_option("--head-data", head_data, "Only take this many lines of data (default: 0 means all)");
	fleet.add_option("--nsamples", nsamples, "How many samples per structure?");
	fleet.add_option("--nstructs", nstructs, "How many structures?");
	fleet.add_option("--polynomial-degree",   polynomial_degree,   "Defaultly -1 means we store everything, otherwise only keep polynomials <= this bound");
	fleet.add_option("--sep", strsep, "Separator for input data (usually space or tab)");
	fleet.add_option("--maxT", maxT, "Max temperature for parallel tempering");
	fleet.add_option("--pt-test-output", pt_test_output, "Should we output only the convenient format for parallel tempering testing?");
	fleet.initialize(argc, argv);
	
	assert(strsep.length()==1);
	sep = strsep.at(0);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the accessors of the variables
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	// must have single digits or checking whether all are use will break
	assert(NUM_VARS < 10); 

	// We'll add these as special functions for each i, so they don't take time in MCTS 
	for(size_t i=0;i<NUM_VARS;i++){
		
		auto l = [=](MyGrammar::VirtualMachineState_t* vms, int) {
			vms->push<D>(vms->xstack.top().at(i));
		};
		
		auto f = new std::function(l);	*f = l;
		
		grammar.add_vms<D>("x"+str(i), f, TERMINAL_P/NUM_VARS);
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the data
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
	// store the x and y values so we can do constant proposals
	std::vector<double> data_x;
	std::vector<double> data_y;
	for(auto v : read_csv(FleetArgs::input_path, false, sep)) { // NOTE: MUST BE \t for our data!!
		
		// get rid of spaces at the end of the line
		while(v[v.size()-1] == "") { v.erase(--v.end()); }
		
		// check that it has the right number of elements
		#if FEYNMAN
				if(v.size() != NUM_VARS + 1) {
				print("## ERROR: v.size() == NUM_VARS+1 Failed", v.size(), NUM_VARS);
				assert(false);
			}
			assert(v.size() == NUM_VARS + 1);
		#else
			if(v.size() != NUM_VARS + 2) {
				print("## ERROR: v.size() == NUM_VARS+2 Failed", v.size(), NUM_VARS);
				assert(false);
			}
		#endif

		X_t x; size_t i=0;
		for(;i<NUM_VARS;i++) {
			x[i] = string_to<D>(v[i]);
			assert(not contains(v[i]," "));
			
			data_x.push_back(x[i]); // all are just counted here in the mean -- maybe not a great idea
		}
		assert(not contains(v[i]," "));
		auto y = string_to<D>(v[i]); i++; 
		assert(not contains(v[i]," "));
		auto sdstr = v[i]; // processed below
		
		// process percentages
		double sd; 
		#if FEYNMAN
			sd = FEYNMAN_SD; // NaN;
		#else
			// process the line 
			if(sdstr[sdstr.length()-1] == '%') sd = y * std::stod(sdstr.substr(0, sdstr.length()-1)) / 100.0; // it's a percentage
			else                               sd = std::stod(sdstr);	
			
			assert(sd > 0.0 && "*** You probably didn't want a zero SD?");
		#endif
				
		if(not pt_test_output)
			COUT "# Data:\t" << x TAB y TAB sd ENDL;
		
		data_y.push_back(y);
		
		mydata.emplace_back(x,y,sdscale*(double)sd);
		
		if(mydata.size() == head_data) break;
		if(CTRL_C) break;
	}
	
	data_X_mean = mean(data_x);
	data_Y_mean = mean(data_y);
	data_X_sd   = sd(data_x);
	data_Y_sd   = sd(data_y);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Run MCTS
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	TopN<MyHypothesis> best(1);

	// if we are on feynman but NOT testing, then make best print
	#if FEYNMAN
		// we will run faster if we only queue the changes
		FleetArgs::MCMCYieldOnlyChanges = true;
		FleetArgs::yieldOnlyChainOne = false; // we want all the chains since we're looking for the best
		
		// for feynman we want to print everything
		if(not pt_test_output) {
			best.print_best = true;
		}
		
		// go through and scale the SDs
		// WE EITHER set the SD above or here
		for(auto& d : mydata) {
			d.reliability = FEYNMAN_SD * data_Y_sd; // Here it's important to scale by data_Y_sd since SDs vary across concepts
		}
	#endif 


	// compute the best possible likelihood
	best_possible_ll = 0.0; 
	for(const auto& d : mydata) {
		best_possible_ll += normal_lpdf(0.0, 0.0, d.reliability);
	}
	print("# Best possible likelihood", best_possible_ll);
	
	#if FEYNMAN
	end_at_likelihood = best_possible_ll - 0.001; // allow a tiny bit of numerical error
	#endif
	
	//	MyHypothesis h0; // NOTE: We do NOT want to sample, since that constrains the MCTS 
	//	MyMCTS m(h0, FleetArgs::explore, &mydata);
	//	for(auto& h: m.run(Control(), h0) | printer(FleetArgs::print, "# "+str(best_possible_ll)+" ")  ) {
		
	//	print("# Initializing parititons...");
//		MyHypothesis h0; 
	//	PartitionMCMC m(h0, FleetArgs::partition_depth, &mydata);	
	//	
	
	auto h0 = MyHypothesis::sample();
	ParallelTempering m(h0, &mydata, FleetArgs::nchains, maxT);
	m.swap_every = 1000; // this seems to matter a lot for Feynman 
	m.adapt_every = 60000;
	for(auto& h: m.run(Control()) | burn(FleetArgs::burn) | printer(FleetArgs::print, "# ") ) { //| show_statistics(10000, m) ) { // (NOTE: If we show_Statistics it will only be that many yielded (diferent) samples)
		
		if(h.posterior == -infinity or std::isnan(h.posterior)) continue; // ignore these
		
		// toss non-linear samples here if we request linearity
		// and non-polynomials (which have NAN values -- NOTE: This is why we have "not" in the next line)
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			continue;
		
		
		best << h;
		
		
		#if !FEYNMAN
		overall_samples << h;
		#endif 
		
		if(end_at_likelihood > -infinity and h.likelihood >= end_at_likelihood) {
			print("Ending on ", h.likelihood,  h.string());
			CTRL_C = true; // kill nicely. 
		}
	}
	
	// print our trees if we want them 
	//	m.print(h0, FleetArgs::tree_path.c_str());
	
	//------------------
	// Postprocessing
	//------------------
	
	// And display!	
	if(pt_test_output) { // special one-line output for testing inference schemes 
		print(FleetArgs::restart, FleetArgs::nchains, maxT, 
			   best.best().posterior, QQ(best.best().structure_string()), QQ(best.best().string()));
		
	}
	else {
		
		print("# Trimming.");
		
		overall_samples.trim(nstructs);

		// figure out the structure normalizer
		double Z = -infinity;
		for(auto& [k,R]: overall_samples) {
			Z = logplusexp(Z, overall_samples.max_posterior(R));
		}
		
		// sort the keys into overall_samples by highest posterior so we print out in order
		std::vector<std::pair<double,std::pair<std::string,double>>> K;
		for(auto& [k,R] : overall_samples) {
			K.emplace_back(overall_samples.max_posterior(R), k);
		}
		std::sort(K.begin(), K.end());
		
		std::cout << std::setprecision(10);
		
		// for computing the f0 distribuiton in case we want it
		//std::vector<std::pair<double,double>> f0distribution;
		
		X_t ones;  ones.fill(1.0);
		X_t zeros; zeros.fill(0.0);	
		COUT "structure\tstructure.max\tweighted.posterior\tposterior\tprior\tlikelihood\tbest.possible.likelihood\tf0\tf1\tpolynomial.degree\th" ENDL;//\tparseable.h" ENDL;
		for(auto& [best_posterior, k] : K) {
			auto& R = overall_samples[k]; // the reservoir sample for k
			
			for(auto h : R.values()) {
				
				// max estimate for structure, uniform for samples within since they're weighted reservoir samples
				double weighted_h_estimate =  ( -log(R.size()) + (best_posterior-Z));
				
				print(  QQ(h.structure_string()),
						 best_posterior,
						 weighted_h_estimate, 
						 h.posterior, h.prior, h.likelihood,
						 best_possible_ll,
						 h.call(zeros, NaN),
						 h.call(ones, NaN),
						 get_polynomial_degree(h.get_value(), h.constants),
						 Q(h.string()));
						 //Q(h.serialize()) 						 

				// for computing the f0 distribution
				//if(get_polynomial_degree(h.get_value(), h.constants) == 1)
				//f0distribution.emplace_back( h.call(ones, NaN) - h.call(zeros, NaN), weighted_h_estimate);
			}
		}
			
			//	auto b = best.best();
	//	for(auto& d : mydata) {
	//		print(d.input, b.call(d.input, NaN), d.output, d.reliability);
	//	}
		
		print("# Best possible likelihood:", best_possible_ll);
		best.print("# Overall best: "+best.best().structure_string()+"\t");
		print("# Overall samples size:" , overall_samples.size());		
		
		
		//auto v25 = weighted_quantile(f0distribution, 0.25);
		//auto v75 = weighted_quantile(f0distribution, 0.75);
		//print(v25, v75, v75-v25);
	}

}

#endif 
