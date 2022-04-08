// NOTE: Currently in MyHypothesis we require it to use ALL the input variables

// we have a "feynman" flag -- when true, we use the feynman grammar, only search for the best, etc. 
//#define FEYNMAN 0

//#define DEBUG_PARTITION_MCMC 1

// if we want to require that hypotheses use all variables
//#define REQUIRE_USE_ALL_VARIABLES 1

#include <cmath>
#include "Numerics.h"

using D = double;

const double sdscale  = 1.0; // can change if we want
int    polynomial_degree = -1; //-1 means do everything, otherwise store ONLY polynomials less than or equal to this bound
double end_at_likelihood = infinity; // if we get this value in log likelihood, we can stop everything (for Feynman)

size_t BURN_N = 0; // 1000; // burn this many at the start of each MCMC chain -- probably NOT needed if doing weighted samples

size_t nsamples = 100; // 100 // how many per structure?
size_t nstructs = 100; //100 // print out all the samples from the top this many structures

const size_t trim_at = 5000; // when we get this big in overall_sample structures
const size_t trim_to = 1000;  // trim to this size

size_t head_data = 0; // if nonzero, this is the max number of lines we'll read in

double fix_sd = -1; // when -1, we won't fix the SD to any particular value

// these are used for some constant proposals
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

class StructuresToSamples : public Singleton<StructuresToSamples>, 
					        public std::map<std::string,PosteriorWeightedReservoirSample<MyHypothesis>> { 
public:

	const size_t trim_at = 5000; // when we get this big in overall_sample structures
	const size_t trim_to = 1000;  // trim to this size
	
	void add(MyHypothesis& h) {
		
		// convert to a structure string
		auto ss = h.structure_string(false); 
		
		// create it if it doesn't exist
		if(not this->count(ss)) { 
			this->emplace(ss,nsamples);
		}
		
		// and add it
		(*this)[ss] << h;
		
		// and trim if we must
		if(this->size() >= trim_at) {
			trim(trim_to);
		}
	}
	void operator<<(MyHypothesis& h) { add(h);}
	
	double max_posterior(const PosteriorWeightedReservoirSample<MyHypothesis>& rs) {
		return max_of(rs.values(), get_posterior<MyHypothesis>).second;
	}

	void trim(const size_t N) {
		
		if(this->size() < N or N <= 0) 
			return;
		
		std::vector<double> structureScores;
		for(auto& [ss, R] : *this) {
			structureScores.push_back( max_posterior(R) );
		}

		// sorts low to high to find the nstructs best's score
		std::sort(structureScores.begin(), structureScores.end(), std::greater<double>());	
		double cutoff = structureScores[N-1];
		
		// now go through and trim
		auto it = this->begin();
		while(it != this->end()) {
			double b = max_posterior(it->second);
			if(b < cutoff) { // if we erase
				it = this->erase(it); // wow, erases returns a new iterator which is valid
			}
			else {
				++it;
			}
		}
	}
	
} overall_samples;

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

#include "PartitionMCMC.h"
#include "FleetArgs.h"
#include "Fleet.h" 

#ifndef DO_NOT_INCLUDE_MAIN

int main(int argc, char** argv){ 
	
	
	// cannot have a likelhood breakout because some likelihoods are positive
	FleetArgs::LIKELIHOOD_BREAKOUT = false; 
	
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
	
//	MyHypothesis h0; // NOTE: We do NOT want to sample, since that constrains the MCTS 
//	MyMCTS m(h0, FleetArgs::explore, &mydata);
//	for(auto& h: m.run(Control(), h0) | print(FleetArgs::print, "# "+str(best_possible_ll)+" ")  ) {
	
//	PRINTN("# Initializing parititons...");
//	MyHypothesis h0; 
//	PartitionMCMC m(h0, FleetArgs::partition_depth, &mydata);	
//	
	auto h0 = MyHypothesis::sample();
	ParallelTempering m(h0, &mydata, 50, 1.5); // 100, 100.0);

//	auto h0 = MyHypothesis::sample();
//	MCMCChain m(h0, &mydata); // 100, 100.0);

	for(auto& h: m.run(Control()) | print(FleetArgs::print, "# ")  ) {
			
		if(h.posterior == -infinity or std::isnan(h.posterior)) continue; // ignore these
		
		// toss non-linear samples here if we request linearity
		// and non-polynomials (NOTE: This is why we have "not" in the next line
		if(polynomial_degree > -1 and not (get_polynomial_degree(h.get_value(), h.constants) <= polynomial_degree)) 
			continue;
		
		best << h;
		
#if !FEYNMAN
		overall_samples << h;
#endif 
		
		if(h.likelihood > end_at_likelihood) {
			CTRL_C = true; // kill nicely. 
		}
	}
	
	// print our trees if we want them 
//	m.print(h0, FleetArgs::tree_path.c_str());
	
	//------------------
	// Postprocessing
	//------------------
	
	COUT "# Trimming." ENDL;
	
	overall_samples.trim(nstructs);

	// figure out the structure normalizer
	double Z = -infinity;
	for(auto& [ss,R]: overall_samples) {
		Z = logplusexp(Z, overall_samples.max_posterior(R));
	}
	
	// sort the keys into overall_samples by highest posterior so we print out in order
	std::vector<std::pair<double,std::string>> K;
	for(auto& [ss,R] : overall_samples) {
		K.emplace_back(overall_samples.max_posterior(R), ss);
	}
	std::sort(K.begin(), K.end());
	
	// And display!	
	X_t ones;  ones.fill(1.0);
	X_t zeros; zeros.fill(0.0);	
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
