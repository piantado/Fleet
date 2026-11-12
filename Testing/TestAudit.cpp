// TestAudit: regression tests for the bugs found in the November 2026 audit of Fleet.
//
// Each section reproduces one bug with Fleet's own code and checks the corrected behavior; the
// comment at the top of each section says what used to happen. The statistical sections compare
// to exactly known answers (the Gamma distribution's moments; the exact posterior of TestModels'
// 309-program grammar).
//
// Anything here that used to HANG is run in a forked child with an alarm, so that a regression
// shows up as a failed check rather than a test that never finishes.

#include "EigenLib.h" // (NewGrammarHypothesis; must come before the rest of Fleet)

#include <set>
#include <sstream>

#include "TestModels.h"
#include "StochasticLOTHypothesis.h"
#include "Lexicon.h"
#include "ChainPool.h"
#include "ParallelTempering.h"
#include "DataTempering.h"
#include "PartitionMCMC.h"
#include "HillClimbing.h"
#include "PriorInference.h"
#include "BasicEnumeration.h"
#include "EnumerationInference.h"
#include "TopN.h"
#include "SampleStreams.h"
#include "ReservoirSample.h"
#include "StreamingStatistics.h"
#include "StreamingSum.h"
#include "CharacterNGram.h"
#include "CachedCallHypothesis.h"
#include "IntegerizedStack.h"
#include "Vector2D.h"
#include "Gamma.h"
#include "NewGrammarHypothesis.h"

using namespace TestHarness;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helpers
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Run f while capturing everything written to std::cout (the tests must print one line only)
template<typename F>
std::string capture_cout(F f) {
	std::cout.flush();
	std::ostringstream ss;
	auto* old = std::cout.rdbuf(ss.rdbuf());
	try { f(); } catch(...) { std::cout.rdbuf(old); throw; }
	std::cout.flush();
	std::cout.rdbuf(old);
	return ss.str();
}

// Does f, run in a forked child, fail to finish cleanly within `seconds`? (crash, throw, or hang)
template<typename F>
bool dies_or_hangs(F f, unsigned seconds=120) {
	return dies([&]{ alarm(seconds); f(); });
}

// A hypothesis on FiniteGrammar whose likelihood is -infinity unless every output is right
class StrictHypothesis : public DeterministicLOTHypothesis<StrictHypothesis, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<StrictHypothesis, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		return call(d.input, -999) == d.output ? 0.0 : -infinity;
	}
};

// FiniteHypothesis, but proposing with regenerate_shallow<D=1>
class ShallowHypothesis : public DeterministicLOTHypothesis<ShallowHypothesis, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<ShallowHypothesis, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		const int out = call(d.input, -999);
		return std::log(ALPHA*(out == d.output) + (1-ALPHA)/NOUT);
	}
	[[nodiscard]] ProposalType propose() const override {
		auto p = Proposals::regenerate_shallow<FiniteGrammar,1>(&finite_grammar, value);
		if(not p) return {};
		return std::make_pair(ShallowHypothesis(std::move(p->first)), p->second);
	}
};

// A grammar in which the lexicon key and the input are the SAME type (int)
class IntGrammar : public Grammar<int,int, int> {
public:
	IntGrammar() {
		add("inc(%s)",    +[](int a) -> int { return a+1; });
		add("add(%s,%s)", +[](int a, int b) -> int { return a+b; });
		add("x",          Builtins::X<IntGrammar>);
		add("F(%s,%s)",   Builtins::LexiconRecurse<IntGrammar,int>);
		add("Fm(%s,%s)",  Builtins::LexiconMemRecurse<IntGrammar,int>);
		add_terminal("0", 0);
		add_terminal("5", 5);
	}
} int_grammar;

class IntInner final : public DeterministicLOTHypothesis<IntInner,int,int,IntGrammar,&int_grammar> {
public:
	using Super = DeterministicLOTHypothesis<IntInner,int,int,IntGrammar,&int_grammar>;
	using Super::Super;
};
class IntStochastic final : public StochasticLOTHypothesis<IntStochastic,int,int,IntGrammar,&int_grammar> {
public:
	using Super = StochasticLOTHypothesis<IntStochastic,int,int,IntGrammar,&int_grammar>;
	using Super::Super;
};
class IntLexicon final : public Lexicon<IntLexicon,int,IntInner,int,int> {
public:
	int run(int key, int x) {
		for(auto& [k,f] : factors) f.program.loader = this;
		return factors.at(key).call(x, -999);
	}
};

// Grammar inference over FiniteHypothesis
using FiniteHD = HumanDatum<FiniteHypothesis>;
class FiniteGH final : public NewGrammarHypothesis<FiniteGH, FiniteHypothesis, FiniteHD> {
public:
	using Super = NewGrammarHypothesis<FiniteGH, FiniteHypothesis, FiniteHD>;
	using Super::Super;
};

// A cached-call hypothesis whose call throws on negative inputs
struct ThrowingCache : public CachedCallHypothesis<ThrowingCache, int, int> {
	int cached_call_wrapper(const int& d) override {
		if(d < 0) throw std::runtime_error("negative");
		return 2*d;
	}
};

// An item with a posterior, for the reservoir sampler
struct Item { double posterior; int id; };

// Mean and its standard error by batch means, for a (correlated) sequence
struct BatchMeans {
	size_t B, per; std::vector<double> sums; size_t n = 0;
	BatchMeans(size_t total, size_t batches=50) : B(batches), per(total/batches), sums(batches, 0.0) {}
	void add(double x) { const size_t k = n/per; if(k < B) sums[k] += x; n++; }
	double mean() const { double m = 0; for(double s : sums) m += s/per; return m/B; }
	double se() const {
		const double m = mean(); double v = 0;
		for(double s : sums) v += (s/per-m)*(s/per-m);
		return std::sqrt(v/(B-1)/B);
	}
};

int main() {
	test_name = "Audit";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("Gamma::propose: MCMC on the prior alone must sample the Gamma distribution");
	// The forward-backward term was -log(s), which is wrong for a Gamma(K,K) multiplier: the chain
	// sampled prior(x)/x, e.g. Gamma(5,5) came out with mean 0.806 instead of 1 (z ~ -190).
	{
		struct Case { float a, b; double mean, meanlog; };
		// E[x] = a/b and E[log x] = digamma(a) - log(b); digamma(2) = 1-gamma, digamma(5) = 25/12-gamma
		const double euler = 0.57721566490153286;
		for(const Case& c : { Case{2, 1, 2.0, 1.0-euler}, Case{5, 5, 1.0, 25.0/12.0-euler-std::log(5.0)} }) {
			const size_t steps = 3000000;
			Gamma h0(c.a, c.b);
			std::vector<NullDatum_t> nodata;
			MCMCChain<Gamma> chain(h0, std::span<NullDatum_t>(nodata));
			BatchMeans bx(steps), bl(steps);
			bool positive = true;
			for(auto& h : chain.run(Control(steps,0,1,0))) {
				bx.add(h.value); bl.add(std::log(h.value));
				if(not (h.value >= 1e-6f)) positive = false;
			}
			note("Gamma(" + str(c.a) + "," + str(c.b) + "): E[x]=" + str(bx.mean()) + " +- " + str(bx.se()) + " (exact " + str(c.mean) +
			     "), E[log x]=" + str(bl.mean()) + " +- " + str(bl.se()) + " (exact " + str(c.meanlog) + ")");
			CHECK_Z((bx.mean()-c.mean)/bx.se(),       "mean of x under Gamma(" << c.a << "," << c.b << ")");
			CHECK_Z((bl.mean()-c.meanlog)/bl.se(),    "mean of log x under Gamma(" << c.a << "," << c.b << ")");
			CHECK(bx.se() < 0.02*c.mean,              "the comparison has power (se of the mean is " << bx.se() << ")");
			CHECK(positive,                           "every sampled value is >= 1e-6");
		}

		// the forward-backward term itself: fb = log q(x'|x) - log q(x|x') for x' = x*s, s ~ Gamma(K,K)
		Gamma g(5,5); g.value = 0.7f;
		bool ok = true; double worst = 0;
		for(int i=0;i<2000;i++) {
			auto p = g.propose();
			if(not p) continue;
			const double x = g.value, y = p->first.value, K = g.PROPOSAL_K;
			const double fwd = gamma_lpdf<double>(y/x, K, K) - std::log(x);
			const double bwd = gamma_lpdf<double>(x/y, K, K) - std::log(y);
			const double err = std::abs(p->second - (fwd-bwd));
			worst = std::max(worst, err);
			if(err > 1e-3) ok = false; // (x and y are floats, so the ratio y/x is s only to ~1e-7; K=200 amplifies that)
		}
		CHECK(ok, "Gamma::propose's fb equals log q(x'|x) - log q(x|x') (worst error " << worst << ")");
	}

	// ==================================================================================
	section("DataTempering: a rejected swap must leave each chain's likelihood alone");
	// The swap test evaluated each chain's current hypothesis on the OTHER chain's data, which
	// overwrote .likelihood; after a rejected swap the chains carried the wrong likelihood.
	{
		std::vector<FiniteHypothesis::datum_t> d;
		for(int i=0;i<12;i++) d.emplace_back((short)(1+i%3), (i%3==2 ? 7 : 2+i%3));
		int bad = 0, trials = 0; unsigned long attempts = 0;
		capture_cout([&]{ // (DataTempering prints its data sizes and its swaps)
			for(int rep=0;rep<20;rep++) {
				FiniteHypothesis h0 = FiniteHypothesis::sample();
				DataTempering<FiniteHypothesis> dt(h0, std::span<FiniteHypothesis::datum_t>(d), 2, 0.5);
				dt.swap_every = 1;
				std::thread sw(&DataTempering<FiniteHypothesis>::__swapper_thread, &dt);
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				dt.terminate = true; sw.join();
				attempts += dt.swap_history.at(1).N;
				for(size_t k=0;k<dt.pool.size();k++) {
					auto cur = dt.pool[k].getCurrent(); // copy
					const double stored_ll = cur.likelihood, stored_post = cur.posterior;
					cur.compute_posterior(dt.pool[k].data);
					trials++;
					if(stored_ll != cur.likelihood or stored_post != cur.posterior) bad++;
				}
			}
		});
		note(str(attempts) + " swap attempts over " + str(trials) + " chains");
		CHECK(attempts >= 20, "the swapper ran (" << attempts << " attempts)");
		CHECK(bad == 0, bad << " of " << trials << " chains hold a likelihood/posterior that is not that of their own data");
	}

	// ==================================================================================
	section("LexiconRecurse / LexiconMemRecurse when key_t == input_t");
	// The key was popped before the argument, but the argument (the last one) is on top: with one
	// shared stack F(key,x) ran factor x on input key. F(0,5) returned 1 instead of 10.
	{
		for(std::string F : {"F", "Fm"}) {
			IntLexicon L;
			L.factors.emplace(0, IntInner("0:add(%s,%s);0:x;0:x"));          // F0(x) = x+x
			L.factors.emplace(5, IntInner("0:inc(%s);0:x"));                 // F5(x) = x+1
			L.factors.emplace(9, IntInner("0:"+F+"(%s,%s);0:0;0:5"));        // F9(x) = F(0, 5)  = F0(5) = 10
			L.factors.emplace(8, IntInner("0:"+F+"(%s,%s);0:5;0:0"));        // F8(x) = F(5, 0)  = F5(0) = 1
			L.factors.emplace(7, IntInner("0:"+F+"(%s,%s);0:5;0:x"));        // F7(x) = F(5, x)  = x+1
			CHECK(L.run(9, 123) == 10, F << "(0,5) must run factor 0 on 5 (=10), got " << L.run(9,123));
			CHECK(L.run(8, 123) == 1,  F << "(5,0) must run factor 5 on 0 (=1), got "  << L.run(8,123));
			CHECK(L.run(7, 41)  == 42, F << "(5,x) must run factor 5 on x, got "      << L.run(7,41));
		}
	}

	// ==================================================================================
	section("burn(m) discards exactly m samples");
	// It discarded m+1.
	{
		for(size_t b : {0, 1, 3, 5, 20, 25}) {
			MCMCChain<FiniteHypothesis> chain(FiniteHypothesis::sample(), finite_data);
			size_t n = 0;
			for(auto& h : chain.run(Control(20,0,1,0)) | burn(b)) { UNUSED(h); n++; }
			CHECK(n == (b >= 20 ? 0 : 20-b), "20 steps | burn(" << b << ") yielded " << n);
		}
	}

	// ==================================================================================
	section("PosteriorWeightedReservoirSample: inclusion probability proportional to weight");
	// With equal weights, N=1000 and capacity 10, the first item was kept 64% of the time and the
	// last 0.1%; both should be 1%.
	{
		const int N = 1000, K = 10, REPS = 40000;
		long first = 0, mid = 0, last = 0; bool sizes = true;
		for(int r=0;r<REPS;r++) {
			PosteriorWeightedReservoirSample<Item> rs(K);
			for(int i=0;i<N;i++) rs << Item{-3.0, i};
			if(rs.size() != (size_t)K) sizes = false;
			for(auto& x : rs.values()) { first += (x.id == 0); mid += (x.id == N/2); last += (x.id == N-1); }
		}
		const double p = (double)K/N;
		CHECK(sizes, "the reservoir holds exactly its capacity");
		CHECK_Z(binomial_z(first, REPS, p), "equal weights: first item kept with probability capacity/N (got " << (double)first/REPS << ")");
		CHECK_Z(binomial_z(mid,   REPS, p), "equal weights: middle item kept with probability capacity/N (got " << (double)mid/REPS << ")");
		CHECK_Z(binomial_z(last,  REPS, p), "equal weights: last item kept with probability capacity/N (got " << (double)last/REPS << ")");

		// unequal weights: item i has weight 2 if i is odd, else 1. For an item late in the stream the
		// inclusion probability is exactly capacity * w_i / (total weight)
		long heavy = 0, light = 0;
		for(int r=0;r<REPS;r++) {
			PosteriorWeightedReservoirSample<Item> rs(K);
			for(int i=0;i<N;i++) rs << Item{(i%2 ? std::log(2.0) : 0.0), i};
			for(auto& x : rs.values()) { heavy += (x.id == N-1); light += (x.id == N-2); }
		}
		const double W = 1.5*N;
		CHECK_Z(binomial_z(heavy, REPS, K*2.0/W), "weight-2 item kept with probability capacity*2/W (got " << (double)heavy/REPS << ")");
		CHECK_Z(binomial_z(light, REPS, K*1.0/W), "weight-1 item kept with probability capacity*1/W (got " << (double)light/REPS << ")");

		// the running total weight must keep growing past 1e6 comparable items
		PosteriorWeightedReservoirSample<Item> big(2);
		for(int i=0;i<3000000;i++) big << Item{0.0, i};
		CHECK_CLOSE(big.weight_lse, std::log(3000000.0), 1e-9, 1e-9, "log total weight of 3e6 unit-weight items");
	}

	// ==================================================================================
	section("StreamingStatistics with non-finite values");
	// N counted nan/inf values but was used as the sample size: {-inf,-inf,10,10} had mean 5, sd 5.77.
	{
		StreamingStatistics s;
		s << -infinity; s << -infinity; s << 10.0; s << 10.0; s << NaN;
		CHECK_CLOSE(s.mean, 10.0, 1e-12, 1e-12, "mean ignores non-finite values");
		CHECK_CLOSE(s.get_sd(), 0.0, 1e-12, 1e-12, "sd ignores non-finite values");
		CHECK(s.N == 5 and s.Nfinite == 2, "N counts every add (" << s.N << "), Nfinite the finite ones (" << s.Nfinite << ")");

		StreamingStatistics t;
		for(double x : {1.0, 2.0, 4.0, 7.0}) t << x;
		t << infinity;
		CHECK_CLOSE(t.mean, 3.5, 1e-12, 1e-12, "mean of {1,2,4,7}");
		CHECK_CLOSE(t.get_variance(), 7.0, 1e-12, 1e-12, "sample variance of {1,2,4,7}");
		CHECK(t.min == 1.0 and t.max == 7.0, "min and max");
		StreamingStatistics u = t; // copy
		CHECK(u.Nfinite == 4 and u.N == 5 and u.mean == t.mean, "a copy carries Nfinite");
	}

	// ==================================================================================
	section("defaultdatum_t::operator== compares count");
	// It compared count == count (itself).
	{
		using D = FiniteHypothesis::datum_t;
		CHECK(not (D((short)1, 2, 0.9, 1.0) == D((short)1, 2, 0.9, 5.0)), "data that differ only in count are not equal");
		CHECK(D((short)1, 2, 0.9, 5.0) == D((short)1, 2, 0.9, 5.0), "identical data are equal");
		CHECK(D((short)1, 2) == D((short)1, 2), "identical data with NaN reliability are equal");
		CHECK(not (D((short)1, 2, 0.9) == D((short)1, 2, 0.8)), "data that differ in reliability are not equal");
	}

	// ==================================================================================
	section("DiscreteDistribution: empty normalizer, and values() for floating point keys");
	// Z() of an empty distribution was 0 (= log 1); values() did not compile for double keys.
	{
		DiscreteDistribution<int> e;
		CHECK(e.Z() == -infinity, "log normalizer of an empty distribution is -infinity, got " << e.Z());

		DiscreteDistribution<double> a, b;
		a.addmass(1.0, std::log(0.25)); a.addmass(2.0, std::log(0.25));
		b.addmass(2.0, std::log(0.25)); b.addmass(3.0, std::log(0.25));
		b << a; // uses values()
		CHECK(b.values().size() == 3, "merged distribution has 3 outcomes");
		CHECK_CLOSE(b.at(2.0), std::log(0.5), 1e-6, 1e-6, "mass of the shared outcome adds");
		CHECK_CLOSE(b.Z(), 0.0, 1e-6, 1e-6, "total mass 1");
	}

	// ==================================================================================
	section("VirtualMachinePool counts each completed state's instructions once");
	// They were added twice.
	{
		for(std::string prog : {"0:inc(%s);0:x", "0:add(%s,%s);0:inc(%s);0:x;0:5"}) {
			IntInner d(prog); IntStochastic s(prog);
			const int dv = d.call(3, -999);
			auto dist = s.call(3, -999);
			CHECK(dist.size() == 1 and dist.count(dv) == 1, "the stochastic call of a deterministic program has its one output");
			CHECK(s.total_instruction_count_last_call == d.total_instruction_count_last_call,
			      prog << ": instruction count " << s.total_instruction_count_last_call << " vs " << d.total_instruction_count_last_call << " from a single VirtualMachineState");
		}
		IntInner fresh;
		CHECK(fresh.total_instruction_count_last_call == 0 and fresh.total_vms_steps == 0, "the counters of a new hypothesis are zero");
	}

	// ==================================================================================
	section("ChainPool can be run more than once");
	// The chains' running states were never reset, so a second step-limited run() spun forever.
	{
		for(auto [nchains, nthreads, steps] : { std::tuple{1,1,150}, std::tuple{3,3,200}, std::tuple{4,2,1050}, std::tuple{2,2,37} }) {
			const bool hung = dies_or_hangs([&, nchains=nchains, nthreads=nthreads, steps=steps]{
				FiniteHypothesis h0 = FiniteHypothesis::sample();
				ChainPool<FiniteHypothesis> pool(h0, finite_data, nchains);
				for(int rep=0;rep<4;rep++) {
					size_t n = 0;
					for(auto& h : pool.run(Control(steps, 0, nthreads, 0))) { UNUSED(h); n++; }
					if(n != (size_t)steps) throw std::runtime_error("wrong number of samples");
				}
			});
			CHECK(not hung, nchains << " chains, " << nthreads << " threads, " << steps << " steps: four runs in a row each finish with exactly " << steps << " samples");
		}

		// and a time-limited run after a step-limited one
		const bool hung = dies_or_hangs([]{
			FiniteHypothesis h0 = FiniteHypothesis::sample();
			ChainPool<FiniteHypothesis> pool(h0, finite_data, 2);
			for(auto& h : pool.run(Control(150, 0, 2, 0))) { UNUSED(h); }
			size_t n = 0;
			for(auto& h : pool.run(Control(0, 200, 2, 0))) { UNUSED(h); n++; }
			if(n == 0) throw std::runtime_error("no samples");
		});
		CHECK(not hung, "a time-limited run after a step-limited run");
	}

	// ==================================================================================
	section("ParallelTempering keeps swapping on later runs");
	// terminate stayed true after the first run, so later runs had no swaps and no adaptation.
	{
		FiniteHypothesis h0 = FiniteHypothesis::sample();
		ParallelTempering<FiniteHypothesis> pt(h0, finite_data, 4, 10.0);
		pt.swap_every = 5;
		for(int rep=0;rep<3;rep++) {
			const unsigned long before = pt.swap_history.at(1).N;
			size_t n = 0;
			for(auto& h : pt.run(Control(0, 600, 2, 0))) { UNUSED(h); n++; }
			const unsigned long attempts = pt.swap_history.at(1).N - before;
			note("run " + str(rep) + ": " + str(n) + " samples, " + str(attempts) + " swap attempts");
			CHECK(attempts > 0, "run " << rep << " attempted swaps");
			CHECK(n > 0, "run " << rep << " yielded samples");
		}

		// adapt() (which reads the chains' sample counts) keeps the ladder's end points and its order
		pt.adapt();
		CHECK(close(pt.pool[0].temperature, 1.0) and close(pt.pool[3].temperature, 10.0), "adapt keeps the end temperatures");
		CHECK(pt.pool[0].temperature < pt.pool[1].temperature and pt.pool[1].temperature < pt.pool[2].temperature
		      and pt.pool[2].temperature < pt.pool[3].temperature, "adapt keeps the ladder increasing");
	}

	// ==================================================================================
	section("PartitionMCMC yields its complete trees on every run");
	{
		FiniteHypothesis h0; // empty
		PartitionMCMC<FiniteHypothesis> samp(h0, 1, finite_data);
		const size_t ncomplete = samp.complete_trees.size();
		CHECK(ncomplete > 0, "depth-1 partition of FiniteGrammar has complete trees (the terminal k)");
		for(int rep=0;rep<2;rep++) {
			size_t n = 0, c = 0;
			for(auto& h : samp.run(Control(200, 0, 1, 0))) { n++; if(h.born_chain_idx == PartitionMCMC<FiniteHypothesis>::COMPLETE_TREE_IDX) c++; }
			CHECK(c == ncomplete, "run " << rep << " yielded " << c << " of the " << ncomplete << " complete trees");
			CHECK(n == 200 + ncomplete, "run " << rep << " yielded " << n << " samples");
		}
	}

	// ==================================================================================
	section("NewGrammarHypothesis: the diagnostic bins when the model is certain");
	// A predicted probability of 1 indexed one past the end of bin_yes/bin_tot (a heap overflow).
	{
		std::vector<FiniteHypothesis> hyps = { FiniteHypothesis("0:k"), FiniteHypothesis("0:s2i(%s);2:sx") };
		std::vector<FiniteHypothesis::datum_t> cond; // twelve observations x -> 7: "k" explains all, "s2i(sx)" none
		for(int i=0;i<12;i++) cond.emplace_back((short)(1+i%3), 7);
		std::vector<float> decays(cond.size(), 1.0f);

		FiniteGH gh(hyps);
		std::map<int,int> resp = {{7, 10}};
		gh.add_HumanData(std::span<FiniteHypothesis::datum_t>(cond), (short)1, resp, decays);
		const double ll = gh.compute_likelihood();
		const size_t nb = gh.bin_tot.size();
		CHECK(nb == 20 and gh.bin_yes.size() == 20, "20 bins");
		CHECK_CLOSE(ll, 0.0, 0, 1e-4, "the model predicts the response 7 with probability ~1");
		CHECK(gh.bin_tot.back() == 10 and gh.bin_yes.back() == 10,
		      "the 10 responses are counted in the last bin (got " << gh.bin_yes.back() << " of " << gh.bin_tot.back() << ")");
		// (the other model response, 1 from s2i(sx), has probability ~0: its 10 trials and 0 yeses are in the first bin)
		int tot = 0, yes = 0; for(int v : gh.bin_tot) tot += v; for(int v : gh.bin_yes) yes += v;
		CHECK(gh.bin_tot.front() == 10 and gh.bin_yes.front() == 0 and tot == 20 and yes == 10, "no other bin was touched");
	}

	// ==================================================================================
	section("HillClimbing from a starting hypothesis with posterior -infinity");
	// TopN does not store such a hypothesis, and run() then called best() on the empty TopN.
	{
		std::vector<StrictHypothesis::datum_t> d = { StrictHypothesis::datum_t((short)1, 2), StrictHypothesis::datum_t((short)2, 3) };
		const bool died = dies_or_hangs([&]{
			StrictHypothesis h0;
			do { h0 = StrictHypothesis::sample(); h0.compute_posterior(d); } while(h0.posterior > -infinity);
			HillClimbing<StrictHypothesis> hc(h0, std::span<StrictHypothesis::datum_t>(d));
			size_t n = 0; double best = -infinity;
			for(auto& h : hc.run(Control(3000,0,1,50))) { n++; best = std::max(best, h.posterior); }
			if(n == 0) throw std::runtime_error("nothing yielded");
			if(not (best > -infinity)) throw std::runtime_error("never found a finite posterior"); // plus(1,sx) fits
		});
		CHECK(not died, "HillClimbing runs, and finds a hypothesis with a finite posterior");
	}

	// ==================================================================================
	section("Iterating a grammar whose first nonterminal type has no rules");
	// begin() did not skip the empty nonterminal: a null dereference and then an endless loop.
	{
		const bool died = dies_or_hangs([]{
			struct G : public Grammar<int,int, bool,double,int> {
				G() { add("x", Builtins::X<G>); add("one", +[]() -> int { return 1; }); }
			} g;
			size_t n = 0; std::set<std::string> seen;
			for(auto& r : g) { seen.insert(r.format); n++; }
			if(n != 2 or seen.size() != 2 or n != g.count_rules()) throw std::runtime_error("wrong rules");
			if(g.get_rule("one")->format != "one") throw std::runtime_error("get_rule");

			struct E : public Grammar<int,int, bool,int> { } e; // no rules at all
			for(auto& r : e) { UNUSED(r); throw std::runtime_error("an empty grammar has no rules"); }
		}, 30);
		CHECK(not died, "iteration visits exactly the rules that exist, and get_rule(string) works");

		size_t n = 0; for(auto& r : finite_grammar) { UNUSED(r); n++; }
		CHECK(n == finite_grammar.count_rules(), "iteration over FiniteGrammar visits all " << finite_grammar.count_rules() << " rules");
	}

	// ==================================================================================
	section("CharacterNGram on strings shorter than n");
	// s.size()-n wrapped around and substr threw std::out_of_range.
	{
		AddLambdaSmoothedNGram ng(3, 26, 0.5);
		CHECK(not throws([&]{ ng.train("ab"); ng.train(""); ng.masked_train("a", '#'); }), "training on short strings does not throw");
		CHECK_CLOSE(ng.probability("abc"), 0.5/(0.5*26), 1e-12, 1e-12, "short strings contribute no n-grams");
		ng.train("abcabd");
		// trigrams: abc, bca, cab, abd; "ab" starts abc and abd
		CHECK_CLOSE(ng.probability("abc"), (1+0.5)/(2+0.5*26), 1e-12, 1e-12, "smoothed P(c | ab)");
	}

	// ==================================================================================
	section("Sampling never falls through on round-off");
	// When uniform() is within a few ulps of 1, rounding could leave the running remainder above
	// zero after the last element, which threw. We force that path by overstating the normalizer.
	{
		std::vector<double> w = {0.3, 0.0, 0.2, 0.5, 0.0};
		auto id = +[](const double& x) -> double { return x; };
		std::function<double(const double&)> idf = id;
		bool ok = true; long tail = 0; const int M = 200000;
		CHECK(not throws([&]{
			for(int i=0;i<M;i++) {
				auto [p1, lp1] = sample<double,std::vector<double>>(w, 1.01, id);   // true z is 1.0
				auto [p2, lp2] = sample<double,std::vector<double>>(w, 1.01, idf);
				if(*p1 == 0.0 or *p2 == 0.0) ok = false; // never a zero-probability element
				tail += (p1 == &w[3]);
			}
		}), "sample() with a normalizer slightly too large does not throw");
		CHECK(ok, "sample() never returns a zero-probability element");
		CHECK_Z(binomial_z(tail, M, (0.5+0.01)/1.01), "the excess goes to the last positive element (" << (double)tail/M << ")");

		auto lid = +[](const double& x) -> double { return std::log(x); };
		std::function<double(const double&)> lidf = lid;
		CHECK(not throws([&]{
			for(int i=0;i<M;i++) {
				auto [p, lp] = sample_lp<double,std::vector<double>>(w, std::log(1.01), lidf);
				if(*p == 0.0) ok = false;
			}
		}), "sample_lp() with a normalizer slightly too large does not throw");
		CHECK(ok, "sample_lp() never returns a zero-probability element");

		// Grammar::sample_rule
		struct G : public Grammar<int,int, int> {
			G() { add("a", +[]() -> int { return 1; }, 0.3); add("b", +[]() -> int { return 2; }, 0.7); add("x", Builtins::X<G>, 0.0); }
		} g;
		g.Z[0] *= 1.01;
		bool rules_ok = true;
		CHECK(not throws([&]{
			for(int i=0;i<M;i++) { if(g.sample_rule(0)->p == 0.0) rules_ok = false; }
		}), "sample_rule with a normalizer slightly too large does not throw");
		CHECK(rules_ok, "sample_rule never returns a zero-probability rule");
	}

	// ==================================================================================
	section("TopN: copying and merging with print_best");
	// A copy added the elements in increasing order, so each was a "new best" and all were printed.
	{
		TopN<FiniteHypothesis> top(10), other(10);
		std::string out = capture_cout([&]{
			top.print_best = true; other.print_best = false;
			for(int i=0;i<200;i++) { auto h = FiniteHypothesis::sample(); h.compute_posterior(finite_data); other << h; }
		});
		CHECK(out.empty(), "nothing is printed without print_best");
		CHECK(other.size() == 10, "filled a TopN");
		other.print_best = true;

		TopN<FiniteHypothesis> c1(1), c3(3);
		out = capture_cout([&]{
			TopN<FiniteHypothesis> c2 = other;         // copy construct
			c1 = other;                                // copy assign
			c3 = TopN<FiniteHypothesis>(other);        // move assign from a copy
			TopN<FiniteHypothesis> c4(std::move(c2));  // move construct
			if(c4.size() != 10) throw std::runtime_error("move");
		});
		CHECK(out.empty(), "copying and moving a TopN prints nothing (printed " << std::count(out.begin(), out.end(), '\n') << " lines)");
		auto keys = [](const TopN<FiniteHypothesis>& t) { std::set<std::string> k; for(auto& h : t.values()) k.insert(key(h.get_value())); return k; };
		CHECK(keys(c1) == keys(other) and c1.N == 10 and c1.print_best, "copy assignment copies the elements, N and print_best");
		CHECK(keys(c3) == keys(other) and c3.N == 10, "move assignment");

		out = capture_cout([&]{ top << other; });
		CHECK(std::count(out.begin(), out.end(), '\n') == 1, "merging into an empty TopN prints the new best once (printed " << std::count(out.begin(), out.end(), '\n') << " lines)");
		CHECK(keys(top) == keys(other), "merging copies every element");
		out = capture_cout([&]{ top << other; });
		CHECK(out.empty(), "merging the same elements again prints nothing");
	}

	// ==================================================================================
	section("Fleet's exceptions report the message they were given");
	// what() returned a fixed string.
	{
		auto what = [](auto f) -> std::string { try { f(); } catch(std::exception& e) { return e.what(); } return "<no exception>"; };
		CHECK(what([]{ throw YouShouldNotBeHereError("*** specific message A"); }) == "*** specific message A", "YouShouldNotBeHereError(msg).what()");
		CHECK(what([]{ throw NotImplementedError("*** specific message B"); }) == "*** specific message B", "NotImplementedError(msg).what()");
		CHECK(not what([]{ throw YouShouldNotBeHereError(); }).empty(), "default YouShouldNotBeHereError has a message");
		CHECK(not what([]{ throw NotImplementedError(); }).empty(), "default NotImplementedError has a message");
	}

	// ==================================================================================
	section("Proposals::regenerate_shallow<D=1>: compiles, and the chain samples the exact posterior");
	// It did not compile. With the reverse move's node-choice probability in fb it satisfies
	// detailed balance, so a chain using only this kernel must reproduce the exact posterior.
	{
		auto states = finite_states();
		auto exact = exact_posterior(states, 1.0);
		const int K = 30;
		auto reps = replicate(K, [](int){
			return run_chain_counts(ShallowHypothesis(finite_grammar.generate()), finite_data, 150000);
		});
		compare_to_exact(reps, exact, "regenerate_shallow<1>");
	}

	// ==================================================================================
	section("Templates that did not compile when used");
	{
		// LOTHypothesis::from_string
		auto h = FiniteHypothesis::from_string(&finite_grammar, "0:plus(%s,%s);2:1;2:sx");
		CHECK(h.string() == FiniteHypothesis("0:plus(%s,%s);2:1;2:sx").string() and h.call(2, -999) == 3, "from_string builds and compiles the hypothesis");

		// BaseNode::map
		Node n = h.get_value();
		size_t visited = 0;
		n.map([&](Node& x){ x.can_resample = false; visited++; });
		bool all = true; for(auto& x : n) if(x.can_resample) all = false;
		CHECK(visited == 3 and all, "map visits and can modify every node");

		// generator | std::set
		std::set<FiniteHypothesis> s;
		MCMCChain<FiniteHypothesis> c(FiniteHypothesis::sample(), finite_data);
		size_t ny = 0;
		for(auto& x : c.run(Control(200,0,1,0)) | s) { UNUSED(x); ny++; }
		CHECK(ny == 200 and s.size() >= 1 and s.size() <= 200, "piping samples into a std::set (" << s.size() << " distinct)");

		// Vector2D::operator()
		Vector2D<double> v(2,3,0.0);
		v(1,2) = 5.0;
		const Vector2D<double>& cv = v;
		CHECK(cv(1,2) == 5.0 and cv(0,0) == 0.0 and v.at(1,2) == 5.0, "Vector2D operator() reads (const) and writes (non-const)");

		// PriorInference and EnumerationInference with a hypothesis type that is not called MyHypothesis
		std::vector<FiniteHypothesis::datum_t> d = finite_data;
		PriorInference<FiniteHypothesis> prinf(d);
		size_t np = 0; bool scored = true;
		for(auto& x : prinf.run(Control(100,0,1,0))) { np++; if(std::isnan(x.posterior)) scored = false; }
		CHECK(np == 100 and scored, "PriorInference yields scored samples from the prior");

		EnumerationInference<FiniteHypothesis, FiniteGrammar, BasicEnumeration<FiniteGrammar>> en(&finite_grammar, finite_data);
		std::set<std::string> es; size_t ne = 0;
		for(auto& x : en.run(Control(4,0,1,0))) { es.insert(key(x.get_value())); ne++; }
		CHECK(ne == 4 and es.size() == 4, "EnumerationInference yields distinct hypotheses (" << es.size() << " of " << ne << ")");
	}

	// ==================================================================================
	section("CachedCallHypothesis: an error from earlier data does not persist");
	{
		ThrowingCache h;
		std::vector<int> bad = {1, -1, 3}, good = {1, 2, 3}, longer = {1, 2, 3, 4};
		h.cached_call(bad);
		CHECK(h.got_error, "an error is recorded");
		h.cached_call(good);
		CHECK(not h.got_error and h.cache == std::vector<int>({2,4,6}), "recomputing on other data clears the error");
		good.push_back(5); // same vector (same address), different size
		h.cached_call(good);
		CHECK(h.cache == std::vector<int>({2,4,6,10}), "a change of size at the same address is noticed");
		h.cached_call(longer);
		CHECK(h.cache == std::vector<int>({2,4,6,8}), "a different vector is recomputed");
	}

	// ==================================================================================
	section("Grammar::change_probability keeps the normalizer exact and later additions sorted");
	{
		struct G : public Grammar<int,int, int> {
			G() {
				add("a", +[]() -> int { return 1; }, 1.0); add("b", +[]() -> int { return 2; }, 2.0);
				add("f(%s)", +[](int a) -> int { return a; }, 3.0);
			}
		} g;
		g.change_probability("a", 1e30);
		g.change_probability("a", 0.5);
		CHECK_CLOSE(g.Z[0], 5.5, 1e-15, 0, "normalizer after moving a probability far away and back");
		CHECK_CLOSE(g.logZ[0], std::log(5.5), 1e-15, 0, "log normalizer");
		g.change_probability("a", 20.0); // the stored order is now b(2), a(20), f: not sorted
		g.change_probability("b", 9.0);
		CHECK(not std::is_sorted(g.rules[0].begin(), g.rules[0].end()), "(setup: change_probability left the rules out of order)");
		g.add("c", +[]() -> int { return 3; }, 4.0);
		CHECK(std::is_sorted(g.rules[0].begin(), g.rules[0].end()), "rules are sorted after adding to a grammar whose probabilities changed");
		CHECK(g.rules[0][0].format == "a" and g.rules[0][1].format == "b" and g.rules[0][2].format == "c" and g.rules[0][3].format == "f(%s)", 
		      "terminals first, by decreasing probability");
		CHECK_CLOSE(g.Z[0], 20.0+9.0+3.0+4.0, 1e-15, 0, "normalizer after adding");
	}

	// ==================================================================================
	section("IntegerizedStack: exact decoding of large integers");
	// floor(sqrt((double)z)) is off by one for some z above 2^53.
	{
		bool ok = true; std::string example;
		auto check = [&](enumerationidx_t z) {
			const auto m = IntegerizedStack::isqrt(z);
			const unsigned __int128 lo = (unsigned __int128)m*m, hi = (unsigned __int128)(m+1)*(m+1);
			if(not (lo <= z and z < hi)) { ok = false; example = "isqrt(" + str(z) + ")"; }
			const auto [x,y] = IntegerizedStack::rosenberg_strong_decode(z);
			if(IntegerizedStack::rosenberg_strong_encode(x,y) != z) { ok = false; example = "decode/encode of " + str(z); }
			enumerationidx_t zz = z; const auto px = IntegerizedStack::rosenberg_strong_pop(zz);
			if(px != x or zz != y) { ok = false; example = "pop of " + str(z); }
		};
		for(enumerationidx_t z=0;z<5000;z++) check(z);
		for(enumerationidx_t m : {(enumerationidx_t)3037000499ull, ((enumerationidx_t)1<<27)+1, ((enumerationidx_t)1<<31)+12345,
		                          ((enumerationidx_t)1<<32)-1, ((enumerationidx_t)1<<30)+7, (enumerationidx_t)94906267ull}) {
			for(enumerationidx_t d=0;d<3;d++) { check(m*m - 1 - d); check(m*m + d); check(m*m + m - 1 + d); check(m*m + 2*m - d); }
		}
		check(std::numeric_limits<enumerationidx_t>::max());
		std::mt19937_64 gen(seed());
		for(int i=0;i<200000;i++) check((enumerationidx_t)gen());
		CHECK(ok, "isqrt and the Rosenberg-Strong pairing are exact for large integers (first failure: " << example << ")");
	}

	// ==================================================================================
	section("StreamingSum: the compensation is applied with the right sign");
	// operator double returned total + low; Kahan's corrected value is total - low.
	{
		StreamingSum<float> s(1.0f);
		for(int i=0;i<30;i++) s << 1e-9f; // each is far below float's resolution at 1.0, so it is all in the compensation
		CHECK_CLOSE((double)s, 1.0 + 30e-9, 0, 2e-9, "1 + 30 * 1e-9 in float, with compensation");

		StreamingSum<double> d;
		for(int i=0;i<1000000;i++) d << 0.1;
		CHECK_CLOSE((double)d, 100000.0, 0, 3e-11, "a million additions of 0.1");
	}

	return finish();
}
