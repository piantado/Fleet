// RebagBenchmark: rebag vs regeneration, at EQUAL COST (likelihood evaluations), with wall time too.
//
//  Part A -- mixing on a model with an exactly known posterior (the 309-program FiniteGrammar model):
//            mean total-variation distance between each chain's visit histogram and the exact
//            posterior, after a fixed number of likelihood evaluations.
//  Part B -- search on arithmetic targets: likelihood evaluations (and seconds) until a chain from
//            the prior first visits a program that fits all data exactly; median over seeds, with
//            the fraction of runs that succeed within the budget.
//
// Methods: regen (Fleet's default proposal), rebag (always), mix (50/50 rebag / regen).
// Cost = calls to compute_likelihood, counted per thread (rebag's internal evaluations of candidate
// trees are included; MCMCChain's evaluation of each proposal is included).
//
// Usage: ./main [seeds=40] [budgetB=300000]

#include "../TestModels.h"
#include "Proposers.h"
#include "RebagProposal.h"
#include <chrono>

inline thread_local unsigned long n_evals = 0;
struct Method { std::string name; double p_rebag; Rebag::Options opt; };
inline thread_local const Method* cur_method = nullptr;

// ---------- Part A model ----------
class FH : public DeterministicLOTHypothesis<FH, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<FH, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	double compute_likelihood(const data_t& data, const double breakout=-infinity) override { n_evals++; return Super::compute_likelihood(data, breakout); }
	double compute_single_likelihood(const datum_t& d) override { return std::log(ALPHA*(call(d.input,-999) == d.output) + (1-ALPHA)/NOUT); }
	[[nodiscard]] ProposalType propose_tempered(const data_t& data, double T) const {
		if(flip(cur_method->p_rebag)) return Proposals::rebag(*this, data, T, cur_method->opt);
		return Super::propose();
	}
};

// ---------- Part B model: integer arithmetic ----------
class AGrammar : public Grammar<int,int, int> {
public:
	AGrammar() {
		add("add(%s,%s)", +[](int a, int b) -> int { return a+b; }, 1.0);
		add("sub(%s,%s)", +[](int a, int b) -> int { return a-b; }, 1.0);
		add("mul(%s,%s)", +[](int a, int b) -> int { return a*b; }, 1.0);
		add("neg(%s)",    +[](int a) -> int { return -a; }, 0.5);
		add("x",          Builtins::X<AGrammar>, 3.0);
		add("one",        +[]() -> int { return 1; }, 1.0);
		add("two",        +[]() -> int { return 2; }, 1.0);
	}
} a_grammar;

constexpr double A_ALPHA = 0.9;
class AH : public DeterministicLOTHypothesis<AH, int, int, AGrammar, &a_grammar> {
public:
	using Super = DeterministicLOTHypothesis<AH, int, int, AGrammar, &a_grammar>;
	using Super::Super;
	double compute_likelihood(const data_t& data, const double breakout=-infinity) override { n_evals++; return Super::compute_likelihood(data, breakout); }
	double compute_single_likelihood(const datum_t& d) override {
		return std::log(A_ALPHA*(call(d.input, -999999) == d.output) + (1-A_ALPHA)/1000.0);
	}
	[[nodiscard]] ProposalType propose_tempered(const data_t& data, double T) const {
		if(flip(cur_method->p_rebag)) return Proposals::rebag(*this, data, T, cur_method->opt);
		return Super::propose();
	}
};

template<typename F>
std::vector<double> parallel_map(int n, F f) {
	std::vector<double> out(n);
	if(std::getenv("REBAG_SERIAL")) { for(int k=0;k<n;k++) out[k] = f(k); return out; } // profiling: run on the main thread
	std::atomic<int> next(0);
	std::vector<std::thread> th;
	const int nt = std::max(1u, std::thread::hardware_concurrency());
	for(int t=0;t<nt;t++) th.emplace_back([&]{ int k; while((k = next++) < n) out[k] = f(k); });
	for(auto& x : th) x.join();
	return out;
}
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); size_t n=v.size(); return n%2 ? v[n/2] : 0.5*(v[n/2-1]+v[n/2]); }

int main(int argc, char** argv) {
	FleetArgs::print_header = 0;
	const int seeds = argc > 1 ? atoi(argv[1]) : 40;
	const unsigned long budgetB = argc > 2 ? atol(argv[2]) : 300000;

	Rebag::Options o; o.max_bag_size = 16; o.enum_max = 2000; o.tries = 0; // pinned: independent of library defaults
	Rebag::Options ot = o; ot.tries = 16;
	Rebag::Options oc = o; oc.cache_capacity = 200000;
	Rebag::Options otc = ot; otc.cache_capacity = 200000;
	std::vector<Method> methods = {
		{"regen", 0.0, o}, {"rebag", 1.0, o}, {"mix+cache", 0.5, oc}, {"mix+tries+c", 0.5, otc},
	};
#ifndef REBAG_V1
	{ // cheap settings: few evaluations per move
		Rebag::Options e = oc; e.enum_max = 8; e.tries = 4;
		methods.push_back({"mix e8t4+c", 0.5, e});
		Rebag::Options el = e; el.scope = Rebag::Scope::Local;
		methods.push_back({"mixloc e8t4+c", 0.5, el});
		Rebag::Options el2 = el; el2.enum_max = 32;
		methods.push_back({"mixloc e32t4+c", 0.5, el2});
		Rebag::Options d; d.cache_capacity = 200000;            // the library defaults (+ cache)
		methods.push_back({"mix defaults+c", 0.5, d});
		Rebag::Options dl = d; dl.scope = Rebag::Scope::Local;
		methods.push_back({"mixloc defaults+c", 0.5, dl});
	}
	Rebag::Options ol = o;  ol.scope = Rebag::Scope::Local;
	Rebag::Options olc = oc; olc.scope = Rebag::Scope::Local;
	methods.push_back({"local", 1.0, ol});
	methods.push_back({"local+cache", 1.0, olc});
	methods.push_back({"mixlocal+c", 0.5, olc});
#endif

	if(argc > 3 and std::string(argv[3]) != "all") {
		auto keep = split(std::string(argv[3]), ',');
		std::vector<Method> m2; for(auto& m : methods) for(auto& k : keep) if(m.name == k) m2.push_back(m);
		methods = m2;
	}
	if(argc > 4 and std::string(argv[4]) == "speed") {
		// throughput: fixed number of MCMC steps on the 10-node target's data, per method
		AH target(a_grammar.from_parseable("0:mul(%s,%s);0:add(%s,%s);0:x;0:two;0:sub(%s,%s);0:mul(%s,%s);0:x;0:x;0:one"));
		std::vector<AH::datum_t> data; for(int xv=-4; xv<=5; xv++) data.push_back(AH::datum_t(xv, target.call(xv, -999999)));
		printf("%-16s %12s %12s %14s %14s\n", "method", "steps", "seconds", "us/step", "evals/step");
		for(auto& m : methods) {
			const unsigned long STEPS = 20000; double secs = 0, evs = 0;
			parallel_map(seeds, [&](int s) -> double {
				cur_method = &m; n_evals = 0; Rebag::clear_cache<AH>();
				auto t0 = std::chrono::steady_clock::now();
				MCMCChain<AH> chain(AH(a_grammar.generate()), data);
				for(auto& h : chain.run(Control(STEPS, 0, 1, 0))) { (void)h; }
				const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
				static std::mutex mx; std::lock_guard lk(mx); secs += dt; evs += n_evals; (void)s; return 0.0;
			});
			printf("%-16s %12lu %12.3f %14.2f %14.2f\n", m.name.c_str(), STEPS*seeds, secs, 1e6*secs/(STEPS*seeds), evs/(STEPS*seeds));
			fflush(stdout);
		}
		return 0;
	}

	// ================= Part A =================
	auto states = finite_states();
	auto exact = exact_posterior(states, 1.0);
	const std::vector<unsigned long> checkpoints = {1000, 3000, 10000, 30000, 100000};
	printf("PART A: FiniteGrammar (309 programs, exact posterior). Mean TV(chain histogram, exact) after E likelihood evaluations, %d chains each\n", seeds);
	printf("%-12s", "method"); for(auto c : checkpoints) printf(" %10lu", c); printf("\n");
	for(auto& m : methods) {
		std::vector<std::vector<double>> tv(checkpoints.size());
		auto res = parallel_map(seeds, [&](int s) -> double {
			cur_method = &m; n_evals = 0; Rebag::clear_cache<FH>(); Rebag::clear_cache<AH>();
			MCMCChain<FH> chain(FH(finite_grammar.generate()), finite_data);
			std::map<std::string,double> c; double tot = 0; size_t ci = 0;
			std::vector<double> mine;
			for(auto& h : chain.run(Control(0, 0, 1, 0))) {
				c[key(h.get_value())]++; tot++;
				while(ci < checkpoints.size() and n_evals >= checkpoints[ci]) {
					double d = 0; for(auto& [k,p] : exact) d += std::abs((c.count(k)?c[k]:0)/tot - p);
					mine.push_back(d/2); ci++;
				}
				if(ci == checkpoints.size()) break;
			}
			static std::mutex mx; std::lock_guard lk(mx);
			for(size_t i=0;i<mine.size();i++) tv[i].push_back(mine[i]);
			(void)s; return 0.0;
		});
		printf("%-12s", m.name.c_str());
		for(auto& v : tv) { double a=0; for(double x : v) a += x; printf(" %10.4f", a/v.size()); }
		printf("\n"); fflush(stdout);
	}

	// ================= Part B =================
	const std::vector<std::string> targets = {
		"0:add(%s,%s);0:mul(%s,%s);0:x;0:x;0:one",                                  // x*x+1            (5)
		"0:mul(%s,%s);0:add(%s,%s);0:x;0:one;0:sub(%s,%s);0:x;0:two",               // (x+1)(x-2)       (7)
		"0:sub(%s,%s);0:mul(%s,%s);0:x;0:x;0:add(%s,%s);0:x;0:two",                 // x*x-(x+2)        (7)
		"0:sub(%s,%s);0:mul(%s,%s);0:x;0:mul(%s,%s);0:x;0:x;0:mul(%s,%s);0:two;0:x",// x^3-2x           (9)
		"0:mul(%s,%s);0:add(%s,%s);0:x;0:two;0:sub(%s,%s);0:mul(%s,%s);0:x;0:x;0:one", // (x+2)(x*x-1)  (10)
	};
	printf("\nPART B: arithmetic targets, data x=-4..5. Evaluations until an exact fit (median over %d seeds, budget %lu evals); success rate; median seconds\n", seeds, budgetB);
	printf("%-40s %-12s %10s %10s %8s %8s   %-17s  %6s\n", "target", "method", "med evals", "med steps", "success", "med sec", "exact/tries/fallbk", "ev/move");
	for(auto& ts : targets) {
		Node tn = a_grammar.from_parseable(ts);
		AH target(tn);
		std::vector<AH::datum_t> data;
		for(int xv=-4; xv<=5; xv++) data.push_back(AH::datum_t(xv, target.call(xv, -999999)));
		target.compute_posterior(data);
		const double best_ll = target.likelihood;
		for(auto& m : methods) {
			std::vector<double> secs(seeds), steps_(seeds); Rebag::Stats agg;
			auto ev = parallel_map(seeds, [&](int s) -> double {
				cur_method = &m; n_evals = 0; Rebag::clear_cache<FH>(); Rebag::clear_cache<AH>();
				auto t0 = std::chrono::steady_clock::now();
				MCMCChain<AH> chain(AH(a_grammar.generate()), data);
				double hit = INFINITY; unsigned long st = 0;
				Rebag::stats = Rebag::Stats{};
				for(auto& h : chain.run(Control(0, 0, 1, 0))) {
					st++;
					if(h.likelihood >= best_ll - 1e-9) { hit = n_evals; break; }
					if(n_evals > budgetB) break;
				}
				{ static std::mutex mx; std::lock_guard lk(mx);
				  steps_[s] = std::isfinite(hit) ? st : INFINITY;
				  auto& S = Rebag::stats; agg.exact += S.exact; agg.tries += S.tries; agg.fallback += S.fallback; agg.identity += S.identity; agg.evaluations += S.evaluations; }
				secs[s] = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
				return hit;
			});
			int succ = 0; for(double e : ev) succ += std::isfinite(e);
			const double nr = agg.exact + agg.tries + agg.fallback + agg.identity;
			printf("%-40s %-12s %10.0f %10.0f %7.0f%% %8.3f   %5.2f/%5.2f/%5.2f  %6.1f\n", tn.string().c_str(), m.name.c_str(), median(ev), median(steps_), 100.0*succ/seeds, median(secs),
			       nr? agg.exact/nr:0, nr? agg.tries/nr:0, nr? agg.fallback/nr:0, (agg.exact+agg.tries)? (double)agg.evaluations/(agg.exact+agg.tries):0);
			fflush(stdout);
		}
	}
	return 0;
}
