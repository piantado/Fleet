#pragma once

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TestModels.h -- small models with EXACTLY known posteriors, shared by TestMCMC,
// TestParallelTempering and TestTopN.
//
// FiniteGrammar has 309 programs in total, so the exact posterior over all of them can be
// computed by enumeration. To keep the reference independent of the code under test:
//   * priors come from a weight table written out separately (FiniteSpec), not from the
//     Grammar's Z/logZ/logp fields, and
//   * likelihoods come from a tree-walking evaluator written here (ref_eval), not from the
//     compiled program / virtual machine.
//
// Chains are compared to the exact posterior with K independent replicate chains: the
// between-chain spread of each state's visit frequency gives a standard error that
// automatically accounts for autocorrelation (no assumptions about mixing time).
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <map>
#include <string>
#include <thread>
#include <vector>

#include "Fleet.h"
#include "Grammar.h"
#include "Builtins.h"
#include "DeterministicLOTHypothesis.h"
#include "MCMCChain.h"

#include "TestHarness.h"

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The independent description of a grammar
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct RuleSpec { int nt; std::string format; std::vector<int> children; double w; };

struct Spec {
	std::vector<RuleSpec> rules;
	double Z(int nt) const { double z = 0; for(auto& r : rules) if(r.nt == nt) z += r.w; return z; }
	const RuleSpec& find(int nt, const std::string& fmt) const {
		for(auto& r : rules) if(r.nt == nt and r.format == fmt) return r;
		throw std::runtime_error("no spec for " + fmt);
	}
	double lp(int nt, const std::string& fmt) const { return std::log(find(nt,fmt).w) - std::log(Z(nt)); }
	double tree_lp(const Node& n) const {
		double s = lp(n.nt(), n.rule->format);
		for(size_t i=0;i<n.nchildren();i++) s += tree_lp(n.child(i));
		return s;
	}
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FiniteGrammar: input short x, output int. Nonterminals: int(0), bool(1), short(2)
//   int   -> plus(short,short) | ite(bool,short,short) | s2i(short) | k
//   bool  -> T | F | lt(short,short)
//   short -> 1 | 2 | 3 | sx          (sx is the input x)
// 4 shorts, 18 bools, and 16 + 18*16 + 4 + 1 = 309 int programs.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class FiniteGrammar : public Grammar<short,int, int,bool,short> {
public:
	FiniteGrammar() {
		add("plus(%s,%s)",    +[](short a, short b) -> int { return a+b; }, 2.0);
		add("ite(%s,%s,%s)",  +[](bool c, short a, short b) -> int { return c ? a : b; }, 1.0);
		add("s2i(%s)",        +[](short a) -> int { return a; }, 1.0);
		add("k",              +[]() -> int { return 7; }, 1.5);
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("F",              +[]() -> bool { return false; }, 2.0);
		add("lt(%s,%s)",      +[](short a, short b) -> bool { return a<b; }, 0.7);
		add("1",              +[]() -> short { return 1; }, 1.0);
		add("2",              +[]() -> short { return 2; }, 1.5);
		add("3",              +[]() -> short { return 3; }, 0.5);
		add("sx",             Builtins::X<FiniteGrammar>, 2.0);
	}
} finite_grammar;

inline const Spec finite_spec = {{
	{0,"plus(%s,%s)",{2,2},2.0}, {0,"ite(%s,%s,%s)",{1,2,2},1.0}, {0,"s2i(%s)",{2},1.0}, {0,"k",{},1.5},
	{1,"T",{},1.0}, {1,"F",{},2.0}, {1,"lt(%s,%s)",{2,2},0.7},
	{2,"1",{},1.0}, {2,"2",{},1.5}, {2,"3",{},0.5}, {2,"sx",{},2.0}
}};

// independent tree-walking evaluator for FiniteGrammar
inline long ref_eval(const Node& n, long x) {
	const std::string& f = n.rule->format;
	if(f == "plus(%s,%s)")   return ref_eval(n.child(0),x) + ref_eval(n.child(1),x);
	if(f == "ite(%s,%s,%s)") return ref_eval(n.child(0),x) ? ref_eval(n.child(1),x) : ref_eval(n.child(2),x);
	if(f == "s2i(%s)")       return ref_eval(n.child(0),x);
	if(f == "k")             return 7;
	if(f == "T")             return 1;
	if(f == "F")             return 0;
	if(f == "lt(%s,%s)")     return ref_eval(n.child(0),x) < ref_eval(n.child(1),x);
	if(f == "1")             return 1;
	if(f == "2")             return 2;
	if(f == "3")             return 3;
	if(f == "sx")            return x;
	throw std::runtime_error("ref_eval: unknown rule " + f);
}

// The likelihood: each datum's output is the program's output with probability ALPHA,
// otherwise uniform over NOUT values
constexpr double ALPHA = 0.6;
constexpr double NOUT  = 20.0;

class FiniteHypothesis : public DeterministicLOTHypothesis<FiniteHypothesis, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<FiniteHypothesis, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		const int out = call(d.input, -999);
		return std::log(ALPHA*(out == d.output) + (1-ALPHA)/NOUT);
	}
};

// shared data (std::span-based data_t must outlive the chains)
inline std::vector<FiniteHypothesis::datum_t> finite_data = {
	FiniteHypothesis::datum_t((short)1, 2), FiniteHypothesis::datum_t((short)2, 3), FiniteHypothesis::datum_t((short)3, 7)
};

inline double ref_loglik(const Node& n, const std::vector<FiniteHypothesis::datum_t>& data) {
	double ll = 0;
	for(auto& d : data) ll += std::log(ALPHA*(ref_eval(n, d.input) == d.output) + (1-ALPHA)/NOUT);
	return ll;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Enumeration and exact posteriors
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// every tree of nonterminal nt with its prior log probability, built from the SPEC
template<typename G>
std::vector<std::pair<Node,double>> enumerate_trees(G& g, const Spec& spec, int nt) {
	std::vector<std::pair<Node,double>> out;
	for(const auto& rs : spec.rules) {
		if(rs.nt != nt or rs.w == 0) continue;
		const double lp = std::log(rs.w) - std::log(spec.Z(nt));
		std::vector<std::pair<Node,double>> partial = { {g.makeNode(g.get_rule((nonterminal_t)nt, rs.format)), lp} };
		for(size_t i=0;i<rs.children.size();i++) {
			auto kids = enumerate_trees(g, spec, rs.children[i]);
			std::vector<std::pair<Node,double>> next;
			for(auto& [p, plp] : partial) for(auto& [k, klp] : kids) {
				Node q = p; q.set_child(i, k);
				next.push_back({q, plp+klp});
			}
			partial = std::move(next);
		}
		for(auto& x : partial) out.push_back(x);
	}
	return out;
}

struct ExactState { Node node; double prior; double loglik; };

// key for a state: the parseable string (no can_resample decorations)
inline std::string key(const Node& n) { return n.parseable(); }

inline std::map<std::string, ExactState> finite_states() {
	std::map<std::string, ExactState> m;
	for(auto& [t, lp] : enumerate_trees(finite_grammar, finite_spec, 0))
		m.emplace(key(t), ExactState{t, lp, ref_loglik(t, finite_data)});
	return m;
}

/**
 * @brief Exact posterior at likelihood temperature T over the states satisfying keep, normalized
 */
inline std::map<std::string,double> exact_posterior(const std::map<std::string, ExactState>& states, double T=1.0,
                                                   std::function<bool(const Node&)> keep = [](const Node&){ return true; }) {
	std::map<std::string,double> lp;
	for(auto& [k, s] : states) if(keep(s.node)) lp[k] = s.prior + s.loglik/T;
	double mx = -INFINITY; for(auto& [k,v] : lp) mx = std::max(mx, v);
	double z = 0; for(auto& [k,v] : lp) z += std::exp(v-mx);
	std::map<std::string,double> out;
	for(auto& [k,v] : lp) out[k] = std::exp(v-mx)/z;
	return out;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Replicate chains and the comparison to an exact distribution
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

using Counts = std::map<std::string,double>;

/**
 * @brief Run K independent replicates of f(k) -> Counts on up to nthreads threads.
 *        Each thread has its own (deterministically seeded, see Rng.h) DefaultRNG.
 */
inline std::vector<Counts> replicate(int K, const std::function<Counts(int)>& f, int nthreads=0) {
	if(nthreads <= 0) nthreads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
	std::vector<Counts> out(K);
	std::atomic<int> next(0);
	std::vector<std::thread> th;
	for(int t=0;t<nthreads;t++) th.emplace_back([&]{ int k; while((k = next++) < K) out[k] = f(k); });
	for(auto& x : th) x.join();
	return out;
}

/**
 * @brief Compare replicate visit counts to an exact distribution.
 *        For every state with exact probability >= minp: t = (mean freq - p) / (between-replicate SE).
 *        With K >= 30 replicates, |t| > 7 has probability < ~1e-8 per state under a correct sampler.
 *        Also checks the pooled total-variation distance and that no state outside the support
 *        is ever visited. Returns the number of failing states.
 */
inline int compare_to_exact(const std::vector<Counts>& reps, const std::map<std::string,double>& exact,
                            const std::string& label, double minp=2e-3, double tmax=7.0) {
	using namespace TestHarness;
	const int K = reps.size();
	std::vector<double> tot(K, 0.0);
	bool outside = false; std::string outside_example;
	for(int k=0;k<K;k++) for(auto& [s,c] : reps[k]) {
		tot[k] += c;
		if(not exact.count(s)) { outside = true; outside_example = s; }
	}
	CHECK(not outside, label << ": visited a state outside the target's support: " << outside_example);
	int bad = 0, tested = 0;
	double tv = 0, tv_noise = 0, worst_t = 0; std::string worst;
	for(auto& [s, p] : exact) {
		double m = 0, m2 = 0, pooled = 0, pooled_n = 0;
		for(int k=0;k<K;k++) {
			const double f = reps[k].count(s) ? reps[k].at(s)/tot[k] : 0.0;
			m += f; m2 += f*f;
			pooled += reps[k].count(s) ? reps[k].at(s) : 0.0; pooled_n += tot[k];
		}
		m /= K;
		const double sd = std::sqrt(std::max(0.0, (m2 - K*m*m)/(K-1)));
		const double se = sd/std::sqrt((double)K);
		tv += std::abs(pooled/pooled_n - p)/2;
		tv_noise += 4*se/2;
		if(p < minp) continue;
		tested++;
		const double t = (se > 0) ? (m - p)/se : (m == p ? 0.0 : INFINITY);
		if(std::abs(t) > std::abs(worst_t)) { worst_t = t; worst = s; }
		if(std::abs(t) > tmax) {
			bad++;
			if(bad <= 5) CHECK(false, label << ": state " << s << " exact p=" << p << " sampled " << m << " (t=" << t << ")");
		}
	}
	note(label + ": tested " + str(tested) + " states, worst t=" + str(worst_t) + " at " + worst +
	     "; TV=" + str(tv) + " (noise scale " + str(tv_noise) + ")");
	CHECK(bad == 0, label << ": " << bad << " of " << tested << " states deviate from the exact distribution");
	CHECK(tv < tv_noise + 1e-3, label << ": total variation " << tv << " exceeds noise scale " << tv_noise);
	return bad;
}

/**
 * @brief Count visits of an MCMCChain run
 */
template<typename H>
Counts run_chain_counts(H h0, typename H::data_t data, size_t steps, double temperature=1.0) {
	MCMCChain<H> chain(h0, data);
	chain.temperature = temperature;
	Counts c;
	for(auto& h : chain.run(Control(steps, 0, 1, 0))) c[key(h.get_value())]++;
	return c;
}
