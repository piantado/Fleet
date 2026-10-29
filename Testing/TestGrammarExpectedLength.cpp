// TestGrammarExpectedLength: GrammarExpectedLength.h -- expected tree size, maximum-entropy rule
// probabilities, and tightness (probability of infinite trees).
//
// References are independent of the header under test:
//   * closed forms for one-nonterminal grammars  S -> f(S,S) [a] | g(S) [b] | x [c],
//   * a long-double fixed-point solver and power iteration written here, from the raw rules,
//   * Monte Carlo through Grammar::generate() (Node::count(), Node::count_terminals()),
//   * a brute-force (ternary search) maximization of the tree entropy for the max-entropy result,
//   * the least fixed point of q = F(q) (iterated from 0) for P(finite tree).

#include <random>

#include "Fleet.h"
#include "EigenLib.h"
#include "Grammar.h"
#include "GrammarExpectedLength.h"

#include "TestHarness.h"

using namespace TestHarness;
namespace GEL = GrammarExpectedLength;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Grammars
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// S -> f(S,S) [a] | g(S) [b] | x [c]:  rho = (2a+b)/Z,  E[count] = 1/(1-rho),  E[terminals] = (c/Z)/(1-rho)
class Binary : public Grammar<int,int, int> {
public:
	Binary(double a, double b, double c) {
		add("f(%s,%s)", +[](int x, int y) -> int { return x+y; }, a);
		add("g(%s)",    +[](int x) -> int { return x; }, b);
		add("x",        +[]() -> int { return 1; }, c);
	}
};

// three types, a fixed catalog of 12 rule shapes; weights are set per instance (0 disables a rule)
using G3 = Grammar<int,int, int,bool,double>;
struct Shape { std::string name; int nt; std::vector<int> kids; };
const std::vector<Shape> SHAPES = {
	{"i0",0,{}}, {"i1(%s)",0,{0}}, {"i2(%s,%s)",0,{0,0}}, {"ib(%s)",0,{1}}, {"id(%s,%s)",0,{2,0}},
	{"b0",1,{}}, {"b1(%s)",1,{1}}, {"bi(%s)",1,{0}}, {"bb(%s,%s)",1,{1,1}},
	{"d0",2,{}}, {"d1(%s)",2,{2}}, {"di(%s,%s)",2,{0,1}},
};
class Random3 : public G3 {
public:
	explicit Random3(const std::vector<double>& w) {
		add("i0",        +[]() -> int { return 0; }, w[0]);
		add("i1(%s)",    +[](int a) -> int { return a; }, w[1]);
		add("i2(%s,%s)", +[](int a, int b) -> int { return a+b; }, w[2]);
		add("ib(%s)",    +[](bool a) -> int { return a; }, w[3]);
		add("id(%s,%s)", +[](double a, int b) -> int { return b; }, w[4]);
		add("b0",        +[]() -> bool { return true; }, w[5]);
		add("b1(%s)",    +[](bool a) -> bool { return not a; }, w[6]);
		add("bi(%s)",    +[](int a) -> bool { return a > 0; }, w[7]);
		add("bb(%s,%s)", +[](bool a, bool b) -> bool { return a and b; }, w[8]);
		add("d0",        +[]() -> double { return 0.5; }, w[9]);
		add("d1(%s)",    +[](double a) -> double { return a; }, w[10]);
		add("di(%s,%s)", +[](int a, bool b) -> double { return a; }, w[11]);
	}
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Independent references, computed from the shape table and weights (not from the Grammar)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct Ref {
	bool empty_reachable = false;      // a reachable type has no positive rule
	std::vector<bool> reach;
	long double rho = 0;               // spectral radius of the reachable mean matrix
	std::vector<long double> E;        // expected cost by type (reachable, subcritical only)
	std::vector<long double> qfin;     // P(finite tree) by type (least fixed point)
};

Ref reference(const std::vector<double>& w, std::function<double(const Shape&)> cost) {
	const int N = 3;
	Ref r;
	std::vector<long double> Z(N, 0);
	for(size_t k=0;k<SHAPES.size();k++) Z[SHAPES[k].nt] += w[k];
	// reachability from type 0 through positive rules
	r.reach.assign(N, false); r.reach[0] = true;
	for(bool changed = true; changed; ) {
		changed = false;
		for(size_t k=0;k<SHAPES.size();k++) if(w[k] > 0 and r.reach[SHAPES[k].nt])
			for(int c : SHAPES[k].kids) if(not r.reach[c]) { r.reach[c] = true; changed = true; }
	}
	for(int i=0;i<N;i++) if(r.reach[i] and Z[i] == 0) r.empty_reachable = true;
	if(r.empty_reachable) return r;
	// mean matrix and expected cost per expansion
	std::vector<std::vector<long double>> M(N, std::vector<long double>(N, 0));
	std::vector<long double> c(N, 0);
	for(size_t k=0;k<SHAPES.size();k++) {
		const int i = SHAPES[k].nt; if(not r.reach[i] or w[k] == 0) continue;
		const long double p = w[k]/Z[i];
		c[i] += p*cost(SHAPES[k]);
		for(int j : SHAPES[k].kids) M[i][j] += p;
	}
	// spectral radius by power iteration on (M + I) (shift makes the Perron root dominant), minus 1
	std::vector<long double> v(N, 1.0L);
	long double lam = 0;
	for(int it=0; it<200000; it++) {
		std::vector<long double> u(N, 0);
		for(int i=0;i<N;i++) if(r.reach[i]) { u[i] = v[i]; for(int j=0;j<N;j++) u[i] += M[i][j]*v[j]; }
		long double mx = 0; for(int i=0;i<N;i++) mx = std::max(mx, u[i]);
		for(int i=0;i<N;i++) v[i] = u[i]/mx;
		if(std::abs(mx - lam) < 1e-18L) { lam = mx; break; }
		lam = mx;
	}
	r.rho = lam - 1;
	// expected cost: x = c + M x by iteration (converges when rho < 1)
	r.E.assign(N, 0);
	if(r.rho < 1 - 1e-6) {
		for(int it=0; it<2000000; it++) {
			long double diff = 0;
			for(int i=0;i<N;i++) if(r.reach[i]) {
				long double x = c[i]; for(int j=0;j<N;j++) x += M[i][j]*r.E[j];
				diff = std::max(diff, std::abs(x - r.E[i])); r.E[i] = x;
			}
			if(diff < 1e-17L*(1+r.E[0])) break;
		}
	}
	// P(finite): least fixed point of q_i = sum_r p_r prod_children q_c, from q = 0
	r.qfin.assign(N, 0);
	for(int it=0; it<2000000; it++) {
		long double diff = 0;
		for(int i=0;i<N;i++) {
			if(Z[i] == 0) continue;
			long double q = 0;
			for(size_t k=0;k<SHAPES.size();k++) if(SHAPES[k].nt == i and w[k] > 0) {
				long double t = w[k]/Z[i]; for(int ch : SHAPES[k].kids) t *= r.qfin[ch]; q += t;
			}
			diff = std::max(diff, std::abs(q - r.qfin[i])); r.qfin[i] = q;
		}
		if(diff < 1e-19L) break;
	}
	return r;
}

double unit_cost(const Shape&) { return 1.0; }
double terminal_cost(const Shape& s) { return s.kids.empty() ? 1.0 : 0.0; }

// entropy (nats) of a one-nonterminal PCFG with probabilities (a,b,c): every node is a draw of the
// same categorical, so H(tree) = E[#nodes] * H(categorical)
double binary_tree_entropy(double a, double b, double c) {
	auto h = [](double p){ return p > 0 ? -p*std::log(p) : 0.0; };
	return (h(a)+h(b)+h(c)) / (1 - (2*a+b));
}

int main() {
	test_name = "GrammarExpectedLength";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("closed forms: S -> f(S,S) [a] | g(S) [b] | x [c]");
	{
		struct C { double a,b,c; };
		for(C k : std::vector<C>{{1,0,4},{3,0,7},{1,1,2},{2,5,3},{0,0,1},{0.0001,0.3,7},{4.5,0,5.5},{1,0.5,2.5}}) {
			Binary g(k.a, k.b, k.c);
			const double Z = k.a+k.b+k.c, rho = (2*k.a+k.b)/Z;
			auto R = GEL::analyze(g);
			CHECK_CLOSE(R.spectral_radius, rho, 1e-12, 1e-14, "rho for " << k.a << "," << k.b << "," << k.c);
			CHECK(R.finite, "finite for rho=" << rho);
			CHECK_CLOSE(R.expected_length, 1/(1-rho), 1e-11, 0, "E[count] = 1/(1-rho) for " << k.a << "," << k.b << "," << k.c);
			CHECK_CLOSE(GEL::expected_length(g), 1/(1-rho), 1e-11, 0, "expected_length()");
			CHECK_CLOSE(GEL::analyze(g, GEL::TerminalCost{}).expected_length, (k.c/Z)/(1-rho), 1e-11, 0, "E[terminals]");
			CHECK_CLOSE(GEL::analyze(g, GEL::InternalCost{}).expected_length, 1/(1-rho) - (k.c/Z)/(1-rho), 1e-10, 1e-12, "E[internal nodes]");
			CHECK_CLOSE(GEL::analyze(g, [](const Rule& r){ return 1.0 + r.N; }).expected_length, (1 + (2*k.a+k.b)/Z)/(1-rho), 1e-11, 0, "custom cost 1+arity");
			CHECK(not GEL::assigns_probability_to_infinite_trees(g), "subcritical is tight");
		}
		// critical: finite trees, infinite mean
		Binary crit(1, 0, 1);
		auto R = GEL::analyze(crit);
		CHECK(not R.finite and R.expected_length == infinity, "critical (rho=1): E = inf");
		auto T = GEL::analyze_tightness(crit);
		CHECK(not T.infinite_trees_possible and T.prob_infinite == 0.0 and T.near_critical, "critical is tight (P(infinite)=0), flagged near_critical");
		// supercritical: P(finite) = (1-q)/q for f(S,S) with prob q
		for(double q : {0.55, 0.6, 0.75, 0.9}) {
			Binary sup(q, 0, 1-q);
			CHECK(GEL::expected_length(sup) == infinity, "supercritical: E = inf");
			auto Ts = GEL::analyze_tightness(sup);
			CHECK(Ts.infinite_trees_possible and Ts.supercritical_reachable, "supercritical has infinite trees (q=" << q << ")");
			CHECK_CLOSE(Ts.prob_infinite, 1 - (1-q)/q, 1e-9, 1e-12, "P(infinite) = 1-(1-q)/q for q=" << q);
		}
		// change_probability is reflected (Z is recomputed from the rules, not read from the cache)
		Binary g(1, 1, 2);
		g.change_probability("f(%s,%s)", 0.5);
		g.change_probability("x", 3.0);
		CHECK_CLOSE(GEL::expected_length(g), 1/(1-(2*0.5+1)/(0.5+1+3)), 1e-12, 0, "after change_probability");
	}

	// ==================================================================================
	section("random 3-type grammars vs independent fixed-point / power iteration");
	{
		std::mt19937_64 rng(seed());
		std::uniform_real_distribution<double> U(0,1);
		int n_finite = 0, n_inf = 0, n_empty = 0, n_bad = 0, n_tight_bad = 0, n_pinf = 0;
		for(int rep=0; rep<1500; rep++) {
			std::vector<double> w(SHAPES.size());
			for(auto& x : w) x = (U(rng) < 0.25) ? 0.0 : std::pow(U(rng), 2) * 3;
			Random3 g(w);
			auto ref = reference(w, unit_cost);
			if(ref.empty_reachable) {
				n_empty++;
				CHECK(throws([&]{ (void)GEL::analyze(g); }), "a reachable type with no positive rule must throw");
				continue;
			}
			if(std::abs((double)ref.rho - 1) < 1e-5) continue; // numerically ambiguous; covered by the closed forms
			auto R = GEL::analyze(g);
			bool ok = close(R.spectral_radius, (double)ref.rho, 1e-9, 1e-9);
			for(int i=0;i<3;i++) ok = ok and R.reachable[i] == ref.reach[i];
			if(ref.rho < 1) {
				n_finite++;
				ok = ok and R.finite and close(R.expected_length, (double)ref.E[0], 1e-9, 1e-12);
				for(int i=0;i<3;i++) if(ref.reach[i]) ok = ok and close(R.expected_length_by_nt[i], (double)ref.E[i], 1e-9, 1e-12);
				for(int i=0;i<3;i++) if(not ref.reach[i]) ok = ok and std::isnan(R.expected_length_by_nt[i]);
				auto refT = reference(w, terminal_cost);
				ok = ok and close(GEL::analyze(g, GEL::TerminalCost{}).expected_length, (double)refT.E[0], 1e-9, 1e-12);
			}
			else { n_inf++; ok = ok and not R.finite and R.expected_length == infinity; }
			if(not ok and n_bad++ < 5) CHECK(false, "random grammar " << str(w) << ": rho " << R.spectral_radius << " vs " << (double)ref.rho
			                                 << ", E " << R.expected_length << " vs " << (double)ref.E[0]);
			// tightness: P(infinite) = 1 - least fixed point of q = F(q)
			auto T = GEL::analyze_tightness(g);
			const double pinf = 1 - (double)ref.qfin[0];
			if(not T.near_critical) {
				const bool tok = close(T.prob_infinite, std::max(0.0, pinf), 1e-7, 1e-7) and T.infinite_trees_possible == (pinf > 1e-7);
				n_pinf += T.infinite_trees_possible;
				if(not tok and n_tight_bad++ < 5) CHECK(false, "tightness of " << str(w) << ": P(inf) " << T.prob_infinite << " vs " << pinf);
				if(ref.rho < 1) CHECK(not T.infinite_trees_possible, "finite expected length implies tight");
			}
		}
		note("grammars: " + str(n_finite) + " subcritical, " + str(n_inf) + " critical/supercritical, " + str(n_empty) +
		     " with an empty reachable type, " + str(n_pinf) + " with P(infinite) > 0");
		CHECK(n_finite > 300 and n_inf > 100 and n_empty > 5 and n_pinf > 50, "random grammars cover all cases");
		CHECK(n_bad == 0, n_bad << " random grammars disagree with the independent reference");
		CHECK(n_tight_bad == 0, n_tight_bad << " random grammars disagree on P(infinite)");
	}

	// ==================================================================================
	section("unreachable and disabled parts of a grammar are ignored");
	{
		// int cannot reach bool or double (ib, id disabled); bool is strongly supercritical (bb dominates)
		std::vector<double> w = {2, 0.3, 0.2, 0, 0,   0.1, 0, 0, 5,   1, 0, 0};
		Random3 g(w);
		auto R = GEL::analyze(g);
		auto ref = reference(w, unit_cost);
		CHECK(not R.reachable[1] and not R.reachable[2] and R.finite and close(R.expected_length, (double)ref.E[0], 1e-12, 0), "unreachable supercritical type is ignored");
		CHECK(not GEL::assigns_probability_to_infinite_trees(g), "unreachable supercritical type does not make the grammar non-tight");
		CHECK(GEL::expected_length(g, (nonterminal_t)1) == infinity, "but starting from the supercritical type, E = inf");
		CHECK(GEL::assigns_probability_to_infinite_trees(g, (nonterminal_t)1), "and it has infinite trees");
	}

	// ==================================================================================
	section("Monte Carlo through Grammar::generate()");
	{
		std::mt19937_64 rng(seed()+1);
		std::uniform_real_distribution<double> U(0,1);
		int tested = 0;
		for(int rep=0; rep<400 and tested<25; rep++) {
			std::vector<double> w(SHAPES.size());
			for(auto& x : w) x = (U(rng) < 0.25) ? 0.0 : U(rng)*3;
			auto ref = reference(w, unit_cost);
			// far from critical, so depth truncation at GRAMMAR_MAX_DEPTH is negligible
			if(ref.empty_reachable or ref.rho > 0.75 or ref.E[0] < 1.5) continue;
			tested++;
			Random3 g(w);
			const double E = GEL::expected_length(g), Et = GEL::analyze(g, GEL::TerminalCost{}).expected_length;
			const int N = 200000;
			double m=0, m2=0, t=0, t2=0;
			for(int i=0;i<N;i++) {
				Node n = g.generate();
				const double c = n.count(), ct = n.count_terminals();
				m += c; m2 += c*c; t += ct; t2 += ct*ct;
			}
			m /= N; t /= N;
			const double se = std::sqrt((m2/N - m*m)/N), set = std::sqrt((t2/N - t*t)/N);
			// (a zero SE means every tree has the same size: then the mean must match exactly)
			if(se > 0) CHECK_Z((m - E)/se, "generate() mean count vs expected_length " << E << " for " << str(w));
			else       CHECK_CLOSE(m, E, 1e-12, 0, "constant tree size vs expected_length");
			if(set > 0) CHECK_Z((t - Et)/set, "generate() mean count_terminals vs TerminalCost " << Et);
			else        CHECK_CLOSE(t, Et, 1e-12, 0, "constant terminal count vs TerminalCost");
		}
		CHECK(tested >= 20, "enough Monte Carlo grammars (" << tested << ")");
	}

	// ==================================================================================
	section("set_max_entropy_probabilities");
	{
		// (1) one nonterminal: maximize H = E[#nodes] * H(a,b,c) subject to E = L by direct search
		for(double L : {1.5, 2.0, 4.0, 10.0, 50.0}) {
			Binary g(1, 1, 1);
			auto m = GEL::set_max_entropy_probabilities(g, L);
			CHECK_CLOSE(GEL::expected_length(g), L, 1e-9, 0, "max-ent achieves E = " << L);
			CHECK_CLOSE(m.achieved_length, L, 1e-9, 0, "achieved_length");
			const double rho = 1 - 1/L; // E = 1/(1-rho)
			// on the constraint, 2a + b = rho and a + b + c = 1, parametrized by a in [max(0,rho-1), rho/2]
			auto H = [&](double a) { const double b = rho - 2*a, c = 1 - a - b; return binary_tree_entropy(a, b, c); };
			double lo = std::max(0.0, rho - 1) , hi = rho/2;
			for(int it=0; it<300; it++) { // H is concave along the constraint line
				const double m1 = lo + (hi-lo)/3, m2 = hi - (hi-lo)/3;
				if(H(m1) < H(m2)) lo = m1; else hi = m2;
			}
			const double a = (lo+hi)/2, b = rho - 2*a, c = 1 - a - b;
			const double Z = g.Z[0];
			CHECK_CLOSE(g.get_rule((nonterminal_t)0, "f(%s,%s)")->p/Z, a, 1e-6, 1e-9, "max-ent p(f) vs brute force, L=" << L);
			CHECK_CLOSE(g.get_rule((nonterminal_t)0, "g(%s)")->p/Z,    b, 1e-6, 1e-9, "max-ent p(g) vs brute force, L=" << L);
			CHECK_CLOSE(g.get_rule((nonterminal_t)0, "x")->p/Z,        c, 1e-6, 1e-9, "max-ent p(x) vs brute force, L=" << L);
			CHECK_CLOSE(m.entropy, H(a), 1e-8, 1e-10, "entropy field vs brute-force maximum, L=" << L);
			// the cached probabilities used by sampling/log_probability were updated consistently
			double z = 0; for(auto& r : g.rules[0]) { z += r.p; CHECK(r.logp == std::log(r.p), "logp in sync"); }
			CHECK_CLOSE(g.Z[0], z, 1e-14, 0, "Z in sync"); CHECK(g.logZ[0] == std::log(g.Z[0]), "logZ in sync");
		}
		// (2) Gibbs form on random multi-type grammars: log P(t) + lambda*count(t) = -log Z_start for EVERY tree
		std::mt19937_64 rng(seed()+2);
		std::uniform_real_distribution<double> U(0,1);
		int tested = 0, gibbs_bad = 0, len_bad = 0;
		for(int rep=0; rep<300 and tested<40; rep++) {
			std::vector<double> w(SHAPES.size());
			for(auto& x : w) x = (U(rng) < 0.2) ? 0.0 : 1.0;
			auto ref = reference(w, unit_cost);
			if(ref.empty_reachable) continue;
			bool unproductive = false; // (documented to throw: some reachable type has no finite tree)
			for(int i=0;i<3;i++) unproductive = unproductive or (ref.reach[i] and ref.qfin[i] == 0);
			if(unproductive) continue;
			Random3 g(w);
			GEL::MaxEntResult m;
			const double L = 3 + U(rng)*6;
			try { m = GEL::set_max_entropy_probabilities(g, L); } catch(std::invalid_argument&) { continue; } // L out of range
			tested++;
			if(not close(GEL::expected_length(g), L, 1e-8, 0)) len_bad++;
			for(int i=0;i<200;i++) {
				Node t = g.generate();
				const double v = g.log_probability(t) + m.lambda*t.count();
				if(not close(v, -m.log_partition, 1e-9, 1e-9)) { gibbs_bad++; break; }
			}
		}
		CHECK(tested >= 20, "enough random max-ent grammars (" << tested << ")");
		CHECK(len_bad == 0, len_bad << " max-ent grammars missed their target expected length");
		CHECK(gibbs_bad == 0, gibbs_bad << " max-ent grammars are not of Gibbs form exp(-lambda*count)/Z");
		// (3) errors leave the grammar unchanged
		Binary g(1, 2, 3);
		const double p0 = g.rules[0][0].p, p1 = g.rules[0][1].p, p2 = g.rules[0][2].p;
		CHECK(throws([&]{ (void)GEL::set_max_entropy_probabilities(g, 1.0); }), "L must exceed the minimum tree size (1)");
		CHECK(throws([&]{ (void)GEL::set_max_entropy_probabilities(g, 5.0, GEL::TerminalCost{}); }), "zero-cost rules are rejected");
		CHECK(g.rules[0][0].p == p0 and g.rules[0][1].p == p1 and g.rules[0][2].p == p2, "failed calls leave the grammar unchanged");
	}

	return finish();
}
