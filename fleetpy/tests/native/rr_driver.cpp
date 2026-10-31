// Native reference for the fleetpy equivalence test: Fleet's own RationalRules model
// (Models/RationalRules/Main.cpp, included verbatim with its main() disabled), run with one
// seeded MCMCChain for a fixed number of steps. Prints the top hypotheses in Fleet's
// parseable form with their exact posterior (as hex floats).
#define NO_INCLUDE_MAIN
#include "../../../Models/RationalRules/Main.cpp"
#include "TopN.h"
#include "MCMCChain.h"
#include "Fleet.h"
#include <cstdio>

int main(int argc, char** argv) {
	Fleet fleet("rr native reference");
	FleetArgs::print_header = 0;
	fleet.initialize(argc, argv);   // takes --seed, --steps, --top

	std::vector<MyHypothesis::datum_t> mydata;
	mydata.emplace_back(MyObject("triangle-blue-1"), true,  0.75);
	mydata.emplace_back(MyObject("circle-blue-1"),   false, 0.75);
	mydata.emplace_back(MyObject("circle-yellow-1"), false, 0.75);

	TopN<MyHypothesis> top(FleetArgs::ntop);
	auto h0 = MyHypothesis::sample();
	MCMCChain samp(h0, mydata);
	unsigned long n = 0;
	uint64_t fnv = 1469598103934665603ULL; // FNV-1a over every sample's parseable string + '\n'
	for(auto& h : samp.run(Control(FleetArgs::steps, 0, 1, 0))) {
		top << h; n++;
		for(unsigned char c : h.get_value().parseable() + "\n") { fnv ^= c; fnv *= 1099511628211ULL; }
	}
	for(auto& h : top.sorted(false)) {
		std::printf("%a\t%a\t%s\t%s\n", h.posterior, h.prior, h.get_value().parseable().c_str(), h.string().c_str());
	}
	std::printf("# samples %lu\n", n);
	std::printf("# trajectory %016llx\n", (unsigned long long)fnv);
	fleet.done = true;
}
