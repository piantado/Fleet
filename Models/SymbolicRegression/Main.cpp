// we have a "feynman" flag -- when true, we use the feynman grammar, only search for the best, etc. 
//#define FEYNMAN 0

#define DEBUG_PARTITION_MCMC 1

// Needed when SDs are small, b/c then they can lead to positive likelihoods
#define NO_BREAKOUT 1

#include <cmath>
#include "Random.h"

using D = double;

const double sdscale  = 1.0; // can change if we want
size_t nsamples = 100; // 100 // how many per structure?
size_t nstructs = 100; //100 // print out all the samples from the top this many structures
int    polynomial_degree = -1; //-1 means do everything, otherwise store ONLY polynomials less than or equal to this bound


size_t BURN_N = 0; // 1000; // burn this many at the start of each MCMC chain -- probably NOT needed if doing weighted samples

const size_t trim_at = 5000; // when we get this big in overall_sample structures
const size_t trim_to = 1000;  // trim to this size

size_t head_data = 0; // if nonzero, this is the max number of lines we'll read in

double fix_sd = -1; // when -1, we won't fix the SD to any particular value

// these are used for some constant proposals
double data_X_mean = NaN;
double data_Y_mean = NaN;
double data_X_sd   = NaN;
double data_Y_sd   = NaN;

const size_t MAX_VARS = 9; // arguments are at most this many 
size_t       NUM_VARS = 1; // how many predictor variables 

// What time do we store our argument as? We use this
// format because it allows multiple variables
using X_t = std::array<D,MAX_VARS>;

char sep = '\t'; // default input separator


const double TERMINAL_P = 2.0;

double best_possible_ll = NaN; // what is the best ll we could have gotten?

double end_at_likelihood = infinity; // if we get this value in log likelihood, we can stop everything (for Feynman)



// Friendly printing of variable names but only the first NUM_VARS of them
std::ostream& operator <<(std::ostream& o, X_t a) {
	
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

MyHypothesis::data_t mydata;

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "Polynomial.h"
#include "ReservoirSample.h"

// keep a big set of samples form structures to the best found for each structure
std::map<std::string,PosteriorWeightedReservoirSample<MyHypothesis>> overall_samples; 

// useful for extracting the max
std::function posterior = [](const MyHypothesis& h) {return h.posterior; };

double max_posterior(const PosteriorWeightedReservoirSample<MyHypothesis>& rs) {
	return max_of(rs.values(), posterior).second;
}

/**
 * @brief This trims overall_samples to the top N different structures with the best scores.
 * 			NOTE: This is a bit inefficient because we really could avoid adding structures until they have
 * 		 		a high enough score (as we do in Top) but then if we did find a good sample, we'd
 * 			    have missed everything before...
 */
void trim_overall_samples(const size_t N) {
	
	if(overall_samples.size() < N or N <= 0) return;
	
	std::vector<double> structureScores;
	for(auto& [ss, R] : overall_samples) {
		structureScores.push_back( max_posterior(R) );
	}

	// sorts low to high to find the nstructs best's score
	std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
	double cutoff = structureScores[N-1];
	
	// now go through and trim
	auto it = overall_samples.begin();
	while(it != overall_samples.end()) {
		double b = max_posterior(it->second);
		if(b < cutoff) { // if we erase
			it = overall_samples.erase(it); // wow, erases returns a new iterator which is valid
		}
		else {
			++it;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "PartitionMCMC.h"
#include "FleetArgs.h"
#include "Fleet.h" 

#ifndef DO_NOT_INCLUDE_MAIN

int main(int argc, char** argv){ 
	
	std::string strsep = "\t";
	
	FleetArgs::inner_timestring = "1m"; // default inner time
	int space_sep=0;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Symbolic regression");
	fleet.add_option("--nvars", NUM_VARS, "How many predictor variables are there?");
	fleet.add_option("--head-data", head_data, "Only take this many lines of data (default: 0 means all)");
	fleet.add_option("--fix-sd", fix_sd, "Should we force the sd to have a particular value?");
	fleet.add_option("--nsamples", nsamples, "How many samples per structure?");
	fleet.add_option("--nstructs", nstructs, "How many structures?");
	fleet.add_option("--space", space_sep, "If 1, our data is space-separated");
	fleet.add_option("--end-at-likelihood", end_at_likelihood, "Stop everything if we make it to this likelihood (presumably, perfect)");
	fleet.add_option("--polynomial-degree",   polynomial_degree,   "Defaultly -1 means we store everything, otherwise only keep polynomials <= this bound");
	fleet.add_option("--sep", strsep, "Separator for input data (usually space or tab)");
	fleet.initialize(argc, argv);
	
	assert(strsep.length()==1);
	sep = strsep.at(0);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// set up the accessors of the variables
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	

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
	best_possible_ll = 0.0;
	
	for(auto v : read_csv(FleetArgs::input_path, false, sep)) { // NOTE: MUST BE \t for our data!!
		
		// get rid of spaces at the end of the line
		while(v[v.size()-1] == "") { v.erase(--v.end()); }
		
		// check that it has the right number of elements
		if(fix_sd == -1) assert(v.size() == NUM_VARS + 2); // must have sd
		else        	 assert(v.size() == NUM_VARS + 1); 
		
		X_t x;
		
		size_t i=0;
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
		if(fix_sd != -1) {
			sd = fix_sd;
			
			// Wow, I *think* if we don't add this amount of noise, then we will tend to overfit, which means that we
			// get stuck in hypotheses and don't search the space very well. I'm not sure why exactly, 
			// but it seems like MCMC just goes to things which are *too* good if it can. 
			//y = y + random_normal(0.0, sd); 
		}
		else {
			// process the line 
			if(sdstr[sdstr.length()-1] == '%') sd = y * std::stod(sdstr.substr(0, sdstr.length()-1)) / 100.0; // it's a percentage
			else                               sd = std::stod(sdstr);								
		}
				
		assert(sd > 0.0 && "*** You probably didn't want a zero SD?");
		
		COUT "# Data:\t" << x TAB y TAB sd ENDL;
		
		best_possible_ll += normal_lpdf(0.0, 0.0, sd);
		data_y.push_back(y);
		
		mydata.emplace_back(x,y,sdscale*(double)sd);
		
		if(mydata.size() == head_data) break;
	}
	
	data_X_mean = mean(data_x);
	data_Y_mean = mean(data_y);
	data_X_sd   = mean(data_x);
	data_Y_sd   = mean(data_y);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Run MCTS
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	TopN<MyHypothesis> best(1);

#if FEYNMAN
	best.print_best = true;
#endif 
	

	//	auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:\u25A0;0:\u25A0");
	//	auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:(%s/%s);0:\u25A0;0:2;0:pow(%s,%s);0:\u25A0;0:\u25A0");
//		auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:(-%s);0:(%s/%s);0:pow(%s,%s);0:\u25A0;0:2;0:2;0:pow(%s,%s);0:\u25A0;0:0.5");
					
		//auto s = grammar.from_parseable("0:(%s/%s);0:exp(%s);0:\u25A0;0:pow(%s,%s);0:\u25A0;0:\u25A0;0:2;0:pow(%s,%s);0:\u25A0;0:\u25A0");

//		auto s = grammar.from_parseable("0:\u25A0");
//		for(auto& n : s) { n.can_resample=false; }
//		
//		grammar.complete(s);
		
//		{
////			MyHypothesis h0(s);
//			MyHypothesis h0 = MyHypothesis::sample();
//			//ParallelTempering m(h0, &mydata, FleetArgs::nchains, 1.1);
//			ParallelTempering m(h0, &mydata, 100, 1000.0 ); //.0, 5.0, 10.0}); //, 100.0, 1000.0}); //5.0, 10.0, 100.0});
////			MCMCChain m(h0, &mydata);
//			for(auto& h: m.run(Control()) | best | print(FleetArgs::print, "# ")  ) {
//			}
//		}	
//		
//		best.print("# Overall best: "+best.best().structure_string()+"\t");
//		return 0;

	
//	MyHypothesis h0; // NOTE: We do NOT want to sample, since that constrains the MCTS 
//	MyMCTS m(h0, FleetArgs::explore, &mydata);
//	for(auto& h: m.run(Control(), h0) | print(FleetArgs::print, "# "+str(best_possible_ll)+" ")  ) {
//	
//	auto h0 = MyHypothesis::sample(); // NOTE: We do NOT want to sample, since that constrains the MCTS 	
//	ParallelTempering m(h0, &mydata, 150, 1.10); // 100, 100.0);
//	MCMCChain m(h0, &mydata); // 100, 100.0);

	MyHypothesis h0; 
	PartitionMCMC m(h0, 3, &mydata);
//	
//	auto cur = get_partitions(h0, 6, 1000);
//	for(auto& h: cur) {
//		PRINTN(h);
//	}
//	
//	return 0;
//			

	for(auto& h: m.run(Control()) | print(FleetArgs::print, "# ")  ) {
			
		if(h.posterior == -infinity or std::isnan(h.posterior)) continue; // ignore these
		
		// toss non-linear samples here if we request linearity
		// and non-polynomials (NOTE: This is why we have "not" in the next line
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			continue;
		
		best << h;
		
#if !FEYNMAN
			// It's important here that we do a structure_string without a dot, because
			// we want to store hypotheses in the same place, even if they came from different MCTS nodes
			auto ss = h.structure_string(false); 
			
			if(not overall_samples.count(ss)) { // create this if it doesn't exist
				overall_samples.emplace(ss,nsamples);
			}
			
			// and add it
			overall_samples[ss] << h;
			
			if(overall_samples.size() >= trim_at) {
				trim_overall_samples(trim_to);
			}
#endif 
		
		if(h.likelihood > end_at_likelihood) {
			CTRL_C = true; // kill nicely. 
		}
	}
	
//	m.print("# Final pool state:");
		
	// print our trees if we want them 
//	m.print(h0, FleetArgs::tree_path.c_str());
	
	//------------------
	// Postprocessing
	//------------------
	
	COUT "# Trimming." ENDL;
	
	trim_overall_samples(nstructs);

	// figure out the structure normalizer
	double Z = -infinity;
	for(auto& [ss,R]: overall_samples) {
		Z = logplusexp(Z, max_posterior(R));
	}
	
	// sort the keys into overall_samples by highest posterior so we print out in order
	std::vector<std::pair<double,std::string>> K;
	for(auto& [ss,R] : overall_samples) {
		K.emplace_back(max_posterior(R), ss);
	}
	std::sort(K.begin(), K.end());
	
	// And display!
	COUT "structure\tstructure.max\tweighted.posterior\tposterior\tprior\tlikelihood\tbest.possible.likelihood\tf0\tf1\tpolynomial.degree\th" ENDL;//\tparseable.h" ENDL;
	for(auto& k : K) {
		double best_posterior = k.first;
		auto& ss = k.second; 
		auto& R = overall_samples[ss]; // the reservoir sample for ss
			
		// find the normalizer for this structure
		double sz = -infinity;
		for(auto& h : R.values()) {
			sz = logplusexp(sz, h.posterior);
		}
		
		X_t ones;  ones.fill(1.0);
		X_t zeros; zeros.fill(0.0);
		
		for(auto h : R.values()) {
			COUT std::setprecision(14) <<
				 QQ(h.structure_string()) TAB 
				 best_posterior TAB 
				 ( (h.posterior-sz) + (best_posterior-Z)) TAB 
				 h.posterior TAB h.prior TAB h.likelihood TAB
				 best_possible_ll TAB
				 h.callOne(zeros, NaN) TAB 
				 h.callOne(ones, NaN) TAB 
				 get_polynomial_degree(h.get_value(), h.constants) TAB 
				 Q(h.string()) // TAB 
				 //Q(h.serialize()) 
				 ENDL;
		}
	}
	
//	auto b = best.best();
//	for(auto& d : mydata) {
//		PRINTN(d.input, b.callOne(d.input, NaN), d.output, d.reliability);
//	}
	
	PRINTN("# Best possible likelihood:", best_possible_ll);
	best.print("# Overall best: "+best.best().structure_string()+"\t");
	PRINTN("# Overall samples size:" , overall_samples.size());
//	PRINTN("# MCTS tree size:", m.count());	
}

#endif 
