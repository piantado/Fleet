// Tests for GrammarExpectedLength::set_max_entropy_probabilities
//
// Independent checks used here:
//  (1) closed forms (S -> f(S,S) | x ; a finite language with negative lambda)
//  (2) brute-force 1-D entropy maximization on a 2-parameter grammar
//  (3) Gibbs property: for every tree t, grammar.log_probability(t) + lambda*cost(t) == -log Z.
//      This is the defining property of the max-entropy solution and uses Fleet's own
//      log_probability (so it also checks the cached logp / logZ we wrote).
//  (4) optimality: for random PCFGs Q on the same grammar, H(Q) <= H*(E_Q[cost]), where H(Q) is
//      computed by an independent recursion (no Eigen); the gap equals KL(Q || P*) >= 0.
//  (5) Monte Carlo of expected length and of entropy (E[-log P(t)]) using Grammar::generate()
//  (6) edge cases, error handling, strong exception guarantee, idempotence, no re-sorting

#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <functional>
#include <map>

#include "EigenLib.h"
#include "Grammar.h"
#include "GrammarExpectedLength.h"

using namespace GrammarExpectedLength;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if(!(cond)) { ++failures; std::cerr << "FAIL [" << __LINE__ << "]: " << msg << std::endl; } } while(0)

static bool close(double a, double b, double rel=1e-9, double abs_=1e-12) {
	if(std::isinf(a) or std::isinf(b)) return a == b;
	return std::abs(a-b) <= abs_ + rel*std::max(std::abs(a), std::abs(b));
}
template<typename F> static bool throws(F f) { try { f(); } catch(const std::exception&) { return true; } return false; }

// snapshot of all probabilities, to test the strong exception guarantee / unchanged parts
template<typename G> std::vector<double> snapshot(const G& g) {
	std::vector<double> v;
	for(size_t i=0;i<G::N_NTs;i++) { for(const auto& r : g.rules[i]) { v.push_back(r.p); v.push_back(r.logp); } v.push_back(g.Z[i]); v.push_back(g.logZ[i]); }
	return v;
}
static bool same(const std::vector<double>& a, const std::vector<double>& b) {
	if(a.size() != b.size()) return false;
	for(size_t i=0;i<a.size();i++) if(!(a[i] == b[i] or (std::isnan(a[i]) and std::isnan(b[i])))) return false;
	return true;
}

template<typename CostF> double tree_cost(const Node& t, CostF cost) { double c = 0; for(const auto& n : t) c += cost(*n.rule); return c; }

// Independent PCFG entropy: h_i = H_i + sum_r p_r sum_children h_c (fixed point, long double)
template<typename G>
long double pcfg_entropy(const G& g, nonterminal_t start) {
	constexpr size_t N = G::N_NTs;
	std::vector<long double> h(N, 0), nh(N);
	for(int it=0; it<1000000; it++) {
		long double md = 0;
		for(size_t i=0;i<N;i++) {
			long double Z = 0; for(const auto& r : g.rules[i]) Z += r.p;
			if(Z == 0) { nh[i] = 0; continue; }
			long double v = 0;
			for(const auto& r : g.rules[i]) {
				if(r.p == 0) continue;
				long double p = r.p / Z, s = -std::log(p);
				for(auto c : r.get_child_types()) s += h[c];
				v += p * s;
			}
			nh[i] = v; md = std::max(md, std::abs(nh[i]-h[i]));
		}
		h.swap(nh);
		if(md < 1e-16L * (1 + std::abs(h[start]))) break;
	}
	return h[start];
}

// ---------------------------------------------------------------------------------------
class Binary : public Grammar<int,int, int> {
public:
	Binary(double a, double b, double c) {
		add("f(%s,%s)", +[](int x, int y) -> int { return x+y; }, a);
		add("g(%s)",    +[](int x) -> int { return x; }, b);
		add("x",        +[]() -> int { return 1; }, c);
	}
};

class RR : public Grammar<double,bool, double,bool> {
public:
	RR() {
		for(int k=0;k<9;k++) add("f"+std::to_string(k)+"(%s)", +[](double x) -> bool { return x > 0; });
		add("and(%s,%s)", +[](bool a, bool b) -> bool { return a and b; });
		add("or(%s,%s)",  +[](bool a, bool b) -> bool { return a or b; });
		add("not(%s)",    +[](bool a) -> bool { return not a; });
		add("x",          +[]() -> double { return 1.0; });
	}
};

using G4 = Grammar<int,int, int,bool,double,std::string>;
class Empty4 : public G4 { public: Empty4() {} };
template<typename T> T dflt() { return T{}; }
template<typename R, typename... A>
void add_rule(G4& g, const std::string& name, double p) {
	std::string fmt = name;
	if constexpr (sizeof...(A) > 0) { fmt += "("; for(size_t i=0;i<sizeof...(A);i++) fmt += (i ? ",%s" : "%s"); fmt += ")"; }
	g.add(fmt, std::function<R(A...)>([](A...) -> R { return dflt<R>(); }), p);
}
using AddF = std::function<void(G4&, const std::string&, double)>;
std::vector<std::tuple<int,std::vector<int>,AddF>> all_shapes() {
	std::vector<std::tuple<int,std::vector<int>,AddF>> v;
	auto per_ret = [&]<typename R>(int ri) {
		v.push_back({ri, {}, [](G4& g, const std::string& n, double p){ add_rule<R>(g,n,p); }});
		auto one = [&]<typename A>(int ai) {
			v.push_back({ri, {ai}, [](G4& g, const std::string& n, double p){ add_rule<R,A>(g,n,p); }});
			auto two = [&]<typename B>(int bi) { v.push_back({ri, {ai,bi}, [](G4& g, const std::string& n, double p){ add_rule<R,A,B>(g,n,p); }}); };
			two.template operator()<int>(0); two.template operator()<bool>(1); two.template operator()<double>(2); two.template operator()<std::string>(3);
		};
		one.template operator()<int>(0); one.template operator()<bool>(1); one.template operator()<double>(2); one.template operator()<std::string>(3);
	};
	per_ret.template operator()<int>(0); per_ret.template operator()<bool>(1); per_ret.template operator()<double>(2); per_ret.template operator()<std::string>(3);
	return v;
}

// Gibbs check on n sampled trees; also returns MC estimates of E[cost] and of entropy
template<typename G, typename CostF>
void gibbs_and_mc(G& g, nonterminal_t start, const MaxEntResult& res, CostF cost, size_t n, const std::string& label, bool check_mc=true) {
	g.GRAMMAR_MAX_DEPTH = 100000;
	double mL=0, mL2=0, mH=0, mH2=0, worst=0;
	for(size_t k=1;k<=n;k++) {
		Node t = g.generate(start);
		double c = tree_cost(t, cost), lp = g.log_probability(t);
		worst = std::max(worst, std::abs(lp + res.lambda*c + res.log_partition) / (1.0 + std::abs(res.log_partition) + std::abs(res.lambda*c)));
		double d = c - mL; mL += d/k; mL2 += d*(c-mL);
		double e = -lp - mH; mH += e/k; mH2 += e*(-lp-mH);
	}
	CHECK(worst < 1e-10, label << ": Gibbs property violated, worst rel dev " << worst);
	if(check_mc) {
		double seL = std::sqrt(mL2/(n-1)/n), seH = std::sqrt(mH2/(n-1)/n);
		CHECK(std::abs(mL - res.target_length) < 5*seL, label << ": MC length " << mL << " +- " << seL << " vs " << res.target_length);
		CHECK(std::abs(mH - res.entropy) < 5*seH,       label << ": MC entropy " << mH << " +- " << seH << " vs " << res.entropy);
	}
}

int main() {
	std::cout << std::setprecision(12);

	// ---------------- (1) closed form: S -> f(S,S) | x ----------------
	// Any PCFG here has E = 1/(1-2q), so the constraint alone fixes q = (1 - 1/L)/2.
	for(double L : {1.5, 2.0, 3.0, 7.0, 25.0, 100.0}) {
		Binary g(1, 0, 1); // g(S) disabled (p=0) -> must stay disabled
		auto res = set_max_entropy_probabilities(g, L);
		double q = (1 - 1/L)/2;
		CHECK(close(g.rules[0][0].p + g.rules[0][1].p + g.rules[0][2].p, 1.0, 1e-12), "normalized");
		double pf = 0, pg = -1, px = 0;
		for(auto& r : g.rules[0]) { if(r.format == "f(%s,%s)") pf = r.p; if(r.format == "g(%s)") pg = r.p; if(r.format == "x") px = r.p; }
		CHECK(close(pf, q, 1e-9) and close(px, 1-q, 1e-9), "binary L=" << L << " pf=" << pf << " expected " << q);
		CHECK(pg == 0.0 and std::isinf(g.rules[0][1].logp) == (g.rules[0][1].p == 0), "disabled rule stays 0");
		CHECK(close(analyze(g).expected_length, L, 1e-9), "binary analyze L");
		CHECK(close(res.achieved_length, L, 1e-9), "achieved");
		// lambda: u = e^-lambda, Z = u + u Z^2, q = uZ  =>  Z = u/(1-q), u^2 = q(1-q)  =>  lambda = -log(q(1-q))/2
		CHECK(close(res.lambda, -0.5*std::log(q*(1-q)), 1e-7), "binary lambda " << res.lambda << " vs " << -0.5*std::log(q*(1-q)));
		CHECK(close(res.entropy, (double)pcfg_entropy(g, 0), 1e-9), "entropy vs independent recursion");
		CHECK(res.min_length == 1.0 and std::isinf(res.max_length), "min/max");
		gibbs_and_mc(g, 0, res, UnitCost{}, L < 30 ? 200000 : 20000, "binary L=" + std::to_string(L), L < 30);
	}

	// ---------------- (2) brute force: S -> f(S,S) | g(S) | x ----------------
	// Constraint: 2 pf + pg = rho := 1 - 1/L. One free parameter pf in [0, rho/2].
	// Tree entropy = (expected #nodes) * h(pf,pg,px) = L * h. Maximize over pf by golden section.
	for(double L : {1.2, 2.0, 4.0, 10.0, 50.0}) {
		double rho = 1 - 1/L;
		auto h = [&](double pf) {
			double pg = rho - 2*pf, px = 1 - rho + pf, s = 0;
			for(double p : {pf,pg,px}) if(p > 0) s -= p*std::log(p);
			return L*s;
		};
		double a = 0, b = rho/2;
		for(int it=0; it<300; it++) { // golden section (h is concave)
			double c = b - (b-a)*0.6180339887498949, d = a + (b-a)*0.6180339887498949;
			if(h(c) > h(d)) b = d; else a = c;
		}
		double pf_bf = (a+b)/2;
		Binary g(1,1,1);
		auto res = set_max_entropy_probabilities(g, L);
		double pf = 0, pg = 0;
		for(auto& r : g.rules[0]) { if(r.format == "f(%s,%s)") pf = r.p; if(r.format == "g(%s)") pg = r.p; }
		CHECK(close(pf, pf_bf, 1e-6, 1e-9), "brute force L=" << L << " pf=" << pf << " vs " << pf_bf);
		CHECK(close(pg, rho - 2*pf_bf, 1e-6, 1e-9), "brute force pg");
		CHECK(res.entropy >= h(pf_bf) - 1e-9 and close(res.entropy, h(pf_bf), 1e-10), "brute force entropy " << res.entropy << " vs " << h(pf_bf));
		gibbs_and_mc(g, 0, res, UnitCost{}, 100000, "fgx L=" + std::to_string(L), L < 20);
	}

	// ---------------- RationalRules-shaped grammar ----------------
	for(double L : {2.5, 3.0, 6.0, 20.0}) {
		RR g;
		const Rule* before = &g.rules[1][0];
		auto order_before = std::vector<std::string>{}; for(auto& r : g.rules[1]) order_before.push_back(r.format);
		auto res = set_max_entropy_probabilities(g, L);
		CHECK(close(analyze(g).expected_length, L, 1e-9), "RR L=" << L << " got " << analyze(g).expected_length);
		CHECK(close(res.entropy, (double)pcfg_entropy(g, g.nt<bool>()), 1e-9), "RR entropy independent");
		CHECK(&g.rules[1][0] == before, "rule storage not reallocated");
		auto order_after = std::vector<std::string>{}; for(auto& r : g.rules[1]) order_after.push_back(r.format);
		CHECK(order_before == order_after, "rules not re-sorted");
		CHECK(close(g.Z[1], 1.0, 1e-12) and close(g.logZ[1], std::log(g.Z[1]), 1e-15), "Z/logZ consistent");
		// symmetric rules must get identical probabilities
		CHECK(g.get_rule("f0(%s)")->p == g.get_rule("f8(%s)")->p, "symmetry f0/f8");
		CHECK(g.get_rule("and(%s,%s)")->p == g.get_rule("or(%s,%s)")->p, "symmetry and/or");
		// idempotence: re-running from the new probabilities gives the same answer
		auto snap = snapshot(g);
		set_max_entropy_probabilities(g, L);
		auto snap2 = snapshot(g); double md = 0; for(size_t i=0;i<snap.size();i++) if(std::isfinite(snap[i])) md = std::max(md, std::abs(snap[i]-snap2[i]));
		CHECK(md < 1e-12, "idempotent, max diff " << md);
		gibbs_and_mc(g, g.nt<bool>(), res, UnitCost{}, 200000, "RR L=" + std::to_string(L), L < 10);
		// non-unit cost: cost = 1 + number of children
		RR g2;
		auto cost2 = [](const Rule& r){ return 1.0 + r.N; };
		auto res2 = set_max_entropy_probabilities(g2, 2*L, cost2);
		CHECK(close(analyze(g2, cost2).expected_length, 2*L, 1e-9), "RR non-unit cost");
		gibbs_and_mc(g2, g2.nt<bool>(), res2, cost2, 100000, "RR cost2 L=" + std::to_string(2*L), L < 10);
	}

	// ---------------- (1b) finite language: negative lambda ----------------
	{
		// bool -> h(obj) | and(obj,obj) ; obj -> x.  Trees: h(x) cost 2, and(x,x) cost 3.
		// Gibbs: P(and)/P(h) = e^{-lambda}; target 2.9 -> P(and)=0.9 -> lambda = -ln 9.
		Empty4 g;
		add_rule<bool,int>(g, "h", 1.0);
		add_rule<bool,int,int>(g, "and", 1.0);
		add_rule<int>(g, "x", 1.0);
		auto res = set_max_entropy_probabilities(g, 2.9, g.nt<bool>());
		CHECK(close(g.get_rule("and(%s,%s)")->p, 0.9, 1e-9), "finite language p(and) " << g.get_rule("and(%s,%s)")->p);
		CHECK(close(res.lambda, -std::log(9.0), 1e-7), "negative lambda " << res.lambda);
		CHECK(res.min_length == 2 and res.max_length == 3, "finite min/max");
		CHECK(throws([&]{ Empty4 h2; add_rule<bool,int>(h2,"h",1); add_rule<bool,int,int>(h2,"and",1); add_rule<int>(h2,"x",1);
		                  set_max_entropy_probabilities(h2, 3.0, h2.nt<bool>()); }), "L >= max throws");
		CHECK(throws([&]{ Empty4 h2; add_rule<bool,int>(h2,"h",1); add_rule<bool,int,int>(h2,"and",1); add_rule<int>(h2,"x",1);
		                  set_max_entropy_probabilities(h2, 2.0, h2.nt<bool>()); }), "L <= min throws");
		gibbs_and_mc(g, g.nt<bool>(), res, UnitCost{}, 100000, "finite language");
	}

	// ---------------- regression: underflow must not disable rules ----------------
	// A target near the minimum makes a costly rule's probability underflow. It used to be stored as
	// exactly 0, which every later call treats as "disabled", so the next result depended on history.
	{
		auto cost = [](const Rule& r){ return r.format == "g(%s)" ? 100.0 : 1.0; };
		Binary g(1,1,1), fresh(1,1,1);
		set_max_entropy_probabilities(g, 1.0 + 1e-10, cost);
		CHECK(g.get_rule("g(%s)")->p > 0.0, "underflowed rule stays enabled");
		set_max_entropy_probabilities(g, 5.0, cost);
		set_max_entropy_probabilities(fresh, 5.0, cost);
		CHECK(close(g.get_rule("g(%s)")->p, fresh.get_rule("g(%s)")->p, 1e-9, 0) and close(g.get_rule("f(%s,%s)")->p, fresh.get_rule("f(%s,%s)")->p, 1e-9),
		      "result independent of call history: " << g.get_rule("g(%s)")->p << " vs " << fresh.get_rule("g(%s)")->p);
	}

	// ---------------- (6) edge cases ----------------
	{
		Empty4 g;
		add_rule<int,int,int>(g, "plus", 1.0);
		add_rule<int>(g, "one", 3.0);
		add_rule<bool,bool,bool>(g, "and", 10.0);   // unreachable from int: must be untouched
		add_rule<bool>(g, "T", 1.0);
		add_rule<int,double>(g, "dis", 0.0);         // disabled rule into an empty nonterminal: stays 0, ok
		auto snap_bool = std::vector<double>{g.get_rule("and(%s,%s)")->p, g.get_rule("T")->p, g.Z[g.nt<bool>()]};
		auto res = set_max_entropy_probabilities(g, 5.0, g.nt<int>());
		CHECK(close(analyze(g, g.nt<int>()).expected_length, 5.0), "edge: target met");
		CHECK(g.get_rule("and(%s,%s)")->p == snap_bool[0] and g.get_rule("T")->p == snap_bool[1] and g.Z[g.nt<bool>()] == snap_bool[2], "unreachable untouched");
		CHECK(res.modified[g.nt<int>()] and not res.modified[g.nt<bool>()], "modified flags");
		CHECK(g.get_rule("dis(%s)")->p == 0.0, "disabled stays disabled");

		// strong exception guarantee
		auto snap = snapshot(g);
		CHECK(throws([&]{ set_max_entropy_probabilities(g, 1.0, g.nt<int>()); }), "L == min throws");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, 0.5, g.nt<int>()); }), "L < min throws");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, -1.0, g.nt<int>()); }), "negative L throws");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, std::nan(""), g.nt<int>()); }), "nan L throws");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, g.nt<int>(), [](const Rule&){ return 0.0; }); }), "zero cost throws");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, g.nt<int>(), TerminalCost{}); }), "terminal-only cost throws (zero on internal)");
		CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, g.nt<double>()); }), "start with no rules throws");
		CHECK(same(snap, snapshot(g)), "grammar unchanged after throws");

		// reachable unproductive nonterminal
		Empty4 u;
		add_rule<int,bool>(u, "ib", 1.0); add_rule<int>(u, "one", 1.0);
		add_rule<bool,bool>(u, "loop", 1.0);         // bool only rewrites to bool: no finite tree
		auto su = snapshot(u);
		CHECK(throws([&]{ set_max_entropy_probabilities(u, 3.0, u.nt<int>()); }), "unproductive throws");
		CHECK(same(su, snapshot(u)), "unchanged after unproductive throw");

		// start symbol with only terminals: max = min -> every target is out of range
		Empty4 t; add_rule<int>(t, "a", 1.0); add_rule<int>(t, "b", 2.0);
		CHECK(throws([&]{ set_max_entropy_probabilities(t, 1.5, t.nt<int>()); }), "terminal-only start throws");

		// integer start chooses the start overload
		Binary b(1,1,1);
		CHECK(close(set_max_entropy_probabilities(b, 4.0, 0).achieved_length, 4.0), "int start overload");
	}

	// ---------------- large targets: conditioning ----------------
	// Near criticality E ~ 1/(1-rho), so the relative error of E is ~ L^2 * (relative error of the
	// stored double probabilities). The function must either meet rel_tol or throw (and leave the
	// grammar unchanged) -- never silently return an inaccurate result.
	for(double L : {1e2, 1e3, 1e4, 1e5, 1e6}) {
		for(double tol : {1e-9, 1e-6, 1e-3}) {
			RR g;
			auto snap = snapshot(g);
			try {
				auto res = set_max_entropy_probabilities(g, L, UnitCost{}, tol);
				double E = analyze(g).expected_length;
				CHECK(close(E, L, tol, 0) and close(res.achieved_length, E, 1e-6), "large L=" << L << " tol=" << tol << " E=" << E);
				std::cout << "large L=" << L << " tol=" << tol << ": ok, |E-L|/L=" << std::abs(E-L)/L << " rho=" << res.spectral_radius << std::endl;
			} catch(const std::runtime_error& e) {
				std::cout << "large L=" << L << " tol=" << tol << ": refused (as allowed)" << std::endl;
				CHECK(same(snap, snapshot(g)), "unchanged after refusal");
				CHECK(L*L*1e-15 > tol, "refused although conditioning should allow it: L=" << L << " tol=" << tol); // L<=1e3 at 1e-9 must work
			}
		}
	}

	// ---------------- (4) optimality on random grammars ----------------
	{
		auto shapes = all_shapes();
		std::mt19937_64 rng(777);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		int tested = 0, skipped = 0, gibbs = 0;
		for(int trial=0; trial<300; trial++) {
			Empty4 g;
			int nr = 0;
			for(int t=0;t<4;t++)
				for(auto& [ri, args, f] : shapes) if(ri == t and args.empty()) { f(g, "t"+std::to_string(t)+"_"+std::to_string(nr++), 0.2 + 3*U(rng)); break; }
			int extra = 2 + (int)(U(rng)*10);
			for(int k=0;k<extra;k++) {
				auto& [ri, args, f] = shapes[(size_t)(U(rng)*shapes.size()) % shapes.size()];
				f(g, "r"+std::to_string(nr++), U(rng) < 0.1 ? 0.0 : U(rng));
			}
			nonterminal_t start = (nonterminal_t)(trial % 4);
			auto A = analyze(g, start);
			if(not A.finite) { skipped++; continue; }

			const double LQ = A.expected_length;
			const double HQ = (double)pcfg_entropy(g, start); // entropy of the random PCFG Q
			auto snap = snapshot(g);
			MaxEntResult res;
			try { res = set_max_entropy_probabilities(g, LQ, start); }
			catch(const std::invalid_argument& e) {
				// only legitimate if Q itself sits at the boundary (all mass on min-cost trees)
				CHECK(same(snap, snapshot(g)), "unchanged after invalid target");
				// Q is at the boundary iff every tree has the same cost (Var = 0); check by sampling
				g.GRAMMAR_MAX_DEPTH = 100000;
				bool constant = true;
				// (compare up to rounding: LQ is computed and may be off by an ulp, e.g. 1 - 2^-53)
				for(int k=0;k<500 and constant;k++) constant = std::abs(tree_cost(g.generate(start), UnitCost{}) - LQ) <= 1e-12 * LQ;
				CHECK(constant, "trial " << trial << " threw but Q has non-constant length: " << e.what());
				skipped++; continue;
			}
			tested++;
			CHECK(close(analyze(g, start).expected_length, LQ, 1e-9), "random trial " << trial << " length");
			CHECK(close(res.entropy, (double)pcfg_entropy(g, start), 1e-8), "random trial " << trial << " entropy independent");
			CHECK(HQ <= res.entropy + 1e-9 * (1 + res.entropy), "OPTIMALITY trial " << trial << ": H(Q)=" << HQ << " > H*=" << res.entropy);
			if(gibbs < 40 and res.spectral_radius < 0.9) { gibbs++; gibbs_and_mc(g, start, res, UnitCost{}, 20000, "random " + std::to_string(trial), false); }
		}
		std::cout << "random optimality: tested=" << tested << " skipped=" << skipped << " gibbs-checked=" << gibbs << std::endl;
		CHECK(tested > 150, "enough random cases");
	}

	std::cout << (failures ? "FAILED " : "ALL PASSED ") << (checks-failures) << "/" << checks << std::endl;
	return failures ? 1 : 0;
}
