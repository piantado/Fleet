// A deterministic, single-threaded benchmark + equivalence harness for 
// Models/FormalLanguageTheory-Simple. 
//
// Why this exists: comparing the speed of two Fleet versions by timing the model's own 
// main is confounded, because (a) the time per MCMC step depends strongly on WHICH 
// hypotheses the chain happens to visit (a chain that wanders into large recursive trees 
// is several times more expensive per step than one sitting on small ones -- across seeds 
// the spread is easily 25%), and (b) the model mains run their chains on worker threads, 
// whose random streams are not guaranteed stable across library versions (and with more 
// than one thread, runs are not reproducible at all -- see the note in ChainPool). 
//
// This harness instead runs ONE MCMCChain on the MAIN thread with a fixed --seed, so two 
// builds do bit-for-bit identical work, and prints every sample. To compare two versions: 
//
//   ./main --seed=42 --steps=60000 --header=0 > trace.txt       # on each version
//   md5sum trace.txt       # identical checksums  => identical sampling behavior
//   time ./main --seed=42 --steps=60000 --header=0 > /dev/null  # => a clean speed A/B
//
// (Run several times and take the minimum; pin to a core with taskset -c 0 if you like.)

#define DO_NOT_INCLUDE_MAIN 1
#include "../../Models/FormalLanguageTheory-Simple/Main.cpp"

#include "TopN.h"
#include "MCMCChain.h"
#include "Fleet.h"
#include "Builtins.h"
#include "VMSRuntimeError.h"

int main(int argc, char** argv){

	Fleet fleet("Deterministic single-threaded benchmark for FormalLanguageTheory-Simple");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use");
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");
	fleet.initialize(argc, argv);

	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 3.0/alphabet.length());
	}

	VirtualMachineControl::MAX_STEPS = 256;
	VirtualMachineControl::MAX_OUTPUTS = 256;

	auto mydata = str_to<std::vector<MyHypothesis::datum_t>>(datastr);
	for(auto& di : mydata) {
		check_alphabet(di.output, alphabet);
	}

	auto h0 = MyHypothesis::sample();

	MCMCChain chain(h0, mydata);

	size_t i = 0;
	for(auto& h : chain.run(Control())) {
		COUT i TAB h.posterior TAB h.prior TAB h.likelihood TAB h.string() ENDL;
		i++;
	}

	return 0;
}
