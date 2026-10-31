// Native reference for fleetpy's Number example: Fleet's own Models/Number-Simple/Main.cpp,
// included verbatim. That file has no main() guard, so its main is renamed here and
// replaced with one seeded MCMCChain on the model's (empty) data. Prints the top hypotheses
// with exact posteriors and an FNV-1a hash over every sample's parseable string.
#define main fleet_number_simple_main
#include "../../../Models/Number-Simple/Main.cpp"
#undef main
#include "MCMCChain.h"
#include <cstdio>

int main(int argc, char** argv) {
	Fleet fleet("number native reference");
	FleetArgs::print_header = 0;
	fleet.initialize(argc, argv);   // --seed, --steps, --top

	MyHypothesis::data_t mydata;    // empty: the likelihood computes its own predictions
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
