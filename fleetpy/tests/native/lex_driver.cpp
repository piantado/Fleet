// Native reference for fleetpy's Lexicon equivalence test: examples/lexicon_arith.py written
// by hand in Fleet C++, the way a Fleet model would be (same rules, probabilities, keys and
// likelihood). Runs one seeded MCMCChain and prints the top hypotheses (exact posteriors) and
// an FNV-1a hash over every sample, where a sample's state string is each factor's
// "key \x1f parseable", joined by \x1e in key order.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<int64_t,int64_t,  int64_t,bool,int>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("zero",          +[]() -> int64_t { return 0; }, 2.0);
		add("inc(%s)",       +[](int64_t a) -> int64_t { return a+1; });
		add("dec(%s)",       +[](int64_t a) -> int64_t { return a-1; });
		add("add(%s,%s)",    +[](int64_t a, int64_t b) -> int64_t { return a+b; });
		add("iszero(%s)",    +[](int64_t a) -> bool { return a==0; });
		add("x",             Builtins::X<MyGrammar>, 3.0);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,int64_t>);
		add("F(%s,%s)",      Builtins::LexiconRecurse<MyGrammar,int>);
		add("Fm(%s,%s)",     Builtins::LexiconMemRecurse<MyGrammar,int>);
		add_terminal("0", int(0), 1.0);
		add_terminal("1", int(1), 1.0);
	}
} grammar;

#include "DeterministicLOTHypothesis.h"
#include "Lexicon.h"

class InnerHypothesis final : public DeterministicLOTHypothesis<InnerHypothesis,int64_t,int64_t,MyGrammar,&grammar> {
public:
	using Super = DeterministicLOTHypothesis<InnerHypothesis,int64_t,int64_t,MyGrammar,&grammar>;
	using Super::Super;
};

class MyHypothesis final : public Lexicon<MyHypothesis,int,InnerHypothesis,int64_t,int64_t> {
public:
	using Super = Lexicon<MyHypothesis,int,InnerHypothesis,int64_t,int64_t>;
	using Super::Super;

	// run factor 1 with this lexicon as the loader for recursive calls; report errors
	int64_t run_main(int64_t x, bool& ok) {
		for(auto& [k,f] : factors) f.program.loader = this;
		const int64_t err = 0;
		InnerHypothesis::VirtualMachineState_t vms(x, err, nullptr);
		vms.program = factors.at(1).program;
		auto out = vms.run();
		ok = (vms.status == vmstatus_t::COMPLETE);
		return out;
	}

	double compute_single_likelihood(const datum_t& di) override {
		bool ok;
		auto out = run_main(di.input, ok);
		return log((1.0-di.reliability)/2.0 + (ok and out == di.output)*di.reliability);
	}
};

#include "TopN.h"
#include "MCMCChain.h"
#include "Fleet.h"

static std::string state(const MyHypothesis& h) {
	std::string out;
	for(auto& [k,f] : h.factors) {
		if(not out.empty()) out += '\x1e';
		out += std::to_string(k) + '\x1f' + f.get_value().parseable();
	}
	return out;
}

int main(int argc, char** argv) {
	Fleet fleet("lexicon native reference");
	FleetArgs::print_header = 0;
	fleet.initialize(argc, argv);   // --seed, --steps, --top

	std::vector<MyHypothesis::datum_t> mydata;
	for(int64_t x=0;x<5;x++) mydata.emplace_back(x, 2*x+1, 0.9);

	TopN<MyHypothesis> top(FleetArgs::ntop);
	auto h0 = MyHypothesis::sample({0,1});
	MCMCChain samp(h0, mydata);
	unsigned long n = 0;
	uint64_t fnv = 1469598103934665603ULL;
	for(auto& h : samp.run(Control(FleetArgs::steps, 0, 1, 0))) {
		top << h; n++;
		for(unsigned char c : state(h) + "\n") { fnv ^= c; fnv *= 1099511628211ULL; }
	}
	for(auto& h : top.sorted(false))
		std::printf("%a\t%a\t%s\n", h.posterior, h.prior, h.string().c_str());
	std::printf("# samples %lu\n", n);
	std::printf("# trajectory %016llx\n", (unsigned long long)fnv);
	fleet.done = true;
}
