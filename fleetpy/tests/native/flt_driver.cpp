// Native reference for fleetpy's stochastic/recursion equivalence test: Fleet's own
// Models/FormalLanguageTheory-Simple (included verbatim, main() disabled), set up exactly as
// its main() does (alphabet terminals, VirtualMachineControl limits, data), then run with one
// seeded MCMCChain for a fixed number of steps. Prints the top hypotheses in parseable form
// with exact posteriors, and an FNV-1a hash over every sample's parseable string.
#define DO_NOT_INCLUDE_MAIN
#include "../../../Models/FormalLanguageTheory-Simple/Main.cpp"
#include "TopN.h"
#include "MCMCChain.h"
#include "Fleet.h"
#include <cstdio>

int main(int argc, char** argv) {
	Fleet fleet("flt native reference");
	FleetArgs::print_header = 0;
	fleet.initialize(argc, argv);   // --seed, --steps, --top

	for(const char c : alphabet)
		grammar.add_terminal(Q(S(1,c)), c, 3.0/alphabet.length());
	VirtualMachineControl::MAX_STEPS = 256;
	VirtualMachineControl::MAX_OUTPUTS = 256;

	auto mydata = str_to<std::vector<MyHypothesis::datum_t>>(datastr);

	TopN<MyHypothesis> top(FleetArgs::ntop);
	auto h0 = MyHypothesis::sample();
	MCMCChain samp(h0, mydata);
	unsigned long n = 0;
	uint64_t fnv = 1469598103934665603ULL;
	for(auto& h : samp.run(Control(FleetArgs::steps, 0, 1, 0))) {
		top << h; n++;
		for(unsigned char c : h.get_value().parseable() + "\n") { fnv ^= c; fnv *= 1099511628211ULL; }
	}
	for(auto& h : top.sorted(false))
		std::printf("%a\t%a\t%s\t%s\n", h.posterior, h.prior, h.get_value().parseable().c_str(), h.string().c_str());
	std::printf("# samples %lu\n", n);
	std::printf("# trajectory %016llx\n", (unsigned long long)fnv);
	fleet.done = true;
}
