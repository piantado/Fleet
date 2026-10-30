// Tests for GrammarExpectedLength::assigns_probability_to_infinite_trees / analyze_tightness
//
// Independent checks:
//  - closed forms: S -> f(S,S) [q] | x [1-q] has P(finite) = min(1, (1-q)/q)
//  - critical grammars are tight (infinite mean but finite trees a.s.)
//  - unproductive nonterminals (exact combinatorial reason) incl. disabled escapes, indirect reach
//  - component structure: critical upstream of critical, subcritical upstream of supercritical, ...
//  - random grammars: decision vs plain fixed-point iteration q <- F(q) from the rules (long double),
//    and vs analyze(): finite expected length implies tight
//  - Monte Carlo with Fleet's generate(): fraction of DepthExceptions at a large depth bound

#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <functional>

#include "EigenLib.h"
#include "Grammar.h"
#include "GrammarExpectedLength.h"

using namespace GrammarExpectedLength;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if(!(cond)) { ++failures; std::cerr << "FAIL [" << __LINE__ << "]: " << msg << std::endl; } } while(0)
static bool close(double a, double b, double tol) { return std::abs(a-b) <= tol; }
template<typename F> static bool throws(F f) { try { f(); } catch(const std::exception&) { return true; } return false; }

// Independent least fixed point of q_i = sum_r p_r prod q_c by plain iteration from 0 (monotone).
template<typename G>
std::vector<long double> reference_q(const G& g, size_t iters) {
	constexpr size_t N = G::N_NTs;
	std::vector<long double> q(N, 0.0L), nq(N);
	for(size_t it=0; it<iters; it++) {
		long double md = 0;
		for(size_t i=0;i<N;i++) {
			long double Z = 0; for(const auto& r : g.rules[i]) Z += r.p;
			if(Z == 0) { nq[i] = 0; continue; }
			long double v = 0;
			for(const auto& r : g.rules[i]) { long double t = r.p / Z; for(auto c : r.get_child_types()) t *= q[c]; v += t; }
			nq[i] = v; md = std::max(md, std::abs(nq[i]-q[i]));
		}
		q.swap(nq);
		if(md < 1e-19L) break;
	}
	return q;
}

class Binary : public Grammar<int,int, int> {
public:
	Binary(double a, double b, double c) {
		add("f(%s,%s)", +[](int x, int y) -> int { return x+y; }, a);
		add("g(%s)",    +[](int x) -> int { return x; }, b);
		add("x",        +[]() -> int { return 1; }, c);
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

int main() {
	std::cout << std::setprecision(12);

	// ---------------- closed form ----------------
	for(double q : {0.1, 0.3, 0.45, 0.5, 0.55, 0.6, 0.75, 0.9, 0.999}) {
		Binary g(q, 0, 1-q);
		auto T = analyze_tightness(g);
		bool expect_inf = q > 0.5;
		double pfin = std::min(1.0, (1-q)/q);
		CHECK(T.infinite_trees_possible == expect_inf, "binary q=" << q << " decision");
		CHECK(assigns_probability_to_infinite_trees(g) == expect_inf, "binary q=" << q << " bool wrapper");
		CHECK(T.near_critical == (q == 0.5), "binary q=" << q << " near_critical");
		CHECK(close(T.prob_infinite, 1-pfin, 1e-9), "binary q=" << q << " P(inf)=" << T.prob_infinite << " vs " << 1-pfin);
		CHECK(close(T.max_component_rho, 2*q, 1e-12), "binary rho");
		CHECK(not T.unproductive_reachable and T.supercritical_reachable == expect_inf, "binary reasons");
		// relation to expected length: critical is tight but has infinite mean
		if(q == 0.5) {
			CHECK(std::isinf(expected_length(g)) and not T.infinite_trees_possible, "critical: tight, infinite mean");
			CHECK(T.prob_infinite == 0.0 and T.prob_infinite_numerical < 1e-7, "critical: exact 0, numerical ~sqrt(eps): " << T.prob_infinite_numerical);
		}
	}

	// ---------------- regression: valid subcritical grammars close to rho = 1 must not throw ----------
	// The internal self-check used to demand numerical P(inf) <= 1e-9 here, which rounding cannot
	// deliver near criticality (it threw at rho = 1 - 1e-8).
	for(int k=3;k<=12;k++) {
		double d = std::pow(10.0, -k), q = (1-d)/2;
		Binary g(q, 0, 1-q);
		TightnessResult T;
		CHECK(not throws([&]{ T = analyze_tightness(g); }), "rho = 1 - 1e-" << k << " threw");
		CHECK(not T.infinite_trees_possible and T.prob_infinite == 0.0, "rho = 1 - 1e-" << k << " tight");
	}

	// ---------------- unproductive (exact) ----------------
	{
		Binary g(0, 1, 0);            // S -> g(S) only : no finite tree
		auto T = analyze_tightness(g);
		CHECK(T.infinite_trees_possible and T.unproductive_reachable and T.prob_infinite == 1.0, "S->g(S): all mass infinite");
		CHECK(not T.supercritical_reachable and T.near_critical, "S->g(S) has rho=1 exactly (singular class)");

		Binary h(0, 1, 0); h.change_probability("x", 0.0); // escape present but disabled
		CHECK(assigns_probability_to_infinite_trees(h), "disabled escape -> unproductive");

		// indirect: int -> b(bool) [0.01] | one ; bool -> loop(bool) only
		Empty4 u;
		add_rule<int,bool>(u, "b", 0.01); add_rule<int>(u, "one", 0.99);
		add_rule<bool,bool>(u, "loop", 1.0);
		auto U = analyze_tightness(u, u.nt<int>());
		CHECK(U.infinite_trees_possible and U.unproductive_reachable, "indirect unproductive");
		CHECK(close(U.prob_infinite, 0.01, 1e-12), "indirect P(inf)=0.01 got " << U.prob_infinite);
		CHECK(U.productive[u.nt<int>()] and not U.productive[u.nt<bool>()], "productive flags");

		// unproductive only via a zero-probability rule: ignored
		Empty4 z;
		add_rule<int,bool>(z, "b", 0.0); add_rule<int>(z, "one", 1.0);
		add_rule<bool,bool>(z, "loop", 1.0);
		CHECK(not assigns_probability_to_infinite_trees(z, z.nt<int>()), "zero-prob path ignored");
		CHECK(assigns_probability_to_infinite_trees(z, z.nt<bool>()), "but from bool itself: infinite");
	}

	// ---------------- component structure ----------------
	{
		// critical int upstream of subcritical bool: tight
		Empty4 a;
		add_rule<int,int,int>(a, "plus", 1.0); add_rule<int,bool>(a, "ofb", 1.0); // int: rho = 2*0.5 = 1
		add_rule<bool,bool,bool>(a, "and", 0.3); add_rule<bool>(a, "T", 0.7);
		auto A = analyze_tightness(a, a.nt<int>());
		CHECK(not A.infinite_trees_possible and A.near_critical, "critical->subcritical tight");
		CHECK(close(A.prob_infinite, 0.0, 1e-7), "critical->subcritical P(inf)~0 got " << A.prob_infinite);

		// critical upstream of critical: tight
		Empty4 b;
		add_rule<int,int,int>(b, "plus", 1.0); add_rule<int,bool>(b, "ofb", 1.0);
		add_rule<bool,bool,bool>(b, "and", 1.0); add_rule<bool>(b, "T", 1.0);
		auto B = analyze_tightness(b, b.nt<int>());
		CHECK(not B.infinite_trees_possible and B.near_critical, "critical->critical tight");
		CHECK(B.prob_infinite == 0.0, "critical->critical exact 0");
		std::cout << "critical->critical raw numerical P(inf) = " << B.prob_infinite_numerical << " (limited to ~eps^(1/4))" << std::endl;
		CHECK(B.prob_infinite_numerical < 1e-3, "critical->critical numerical small");

		// subcritical int upstream of supercritical bool: NOT tight, and P(inf) = P(reach bool)*P_bool(inf)
		Empty4 c;
		add_rule<int,bool>(c, "ofb", 0.2); add_rule<int>(c, "one", 0.8);
		add_rule<bool,bool,bool>(c, "and", 0.75); add_rule<bool>(c, "T", 0.25);   // P_bool(finite) = 1/3
		auto C = analyze_tightness(c, c.nt<int>());
		CHECK(C.infinite_trees_possible and C.supercritical_reachable and not C.unproductive_reachable, "sub->super not tight");
		CHECK(close(C.prob_infinite, 0.2*(2.0/3.0), 1e-12), "sub->super P(inf)=" << C.prob_infinite);
		CHECK(close(C.component_rho_by_nt[c.nt<int>()], 0.0, 0) and close(C.component_rho_by_nt[c.nt<bool>()], 1.5, 1e-12), "component rhos");

		// supercritical part unreachable: tight
		Empty4 d;
		add_rule<int>(d, "one", 1.0);
		add_rule<bool,bool,bool>(d, "and", 0.9); add_rule<bool>(d, "T", 0.1);
		CHECK(not assigns_probability_to_infinite_trees(d, d.nt<int>()), "unreachable supercritical ignored");
		CHECK(std::isnan(analyze_tightness(d, d.nt<int>()).prob_finite_by_nt[d.nt<bool>()]), "unreachable q is NaN");

		// mutual recursion: int -> i_of_b(bool) ; bool -> b_of_ii(int,int) [p] | T ; rho = sqrt(2p)
		for(double p : {0.3, 0.5, 0.7}) {
			Empty4 e;
			add_rule<int,bool>(e, "i_of_b", 1.0);
			add_rule<bool,int,int>(e, "b_of_ii", p); add_rule<bool>(e, "T", 1-p);
			auto E = analyze_tightness(e, e.nt<int>());
			CHECK(close(E.max_component_rho, std::sqrt(2*p), 1e-12), "cycle rho");
			CHECK(E.infinite_trees_possible == (p > 0.5) and E.near_critical == (p == 0.5), "cycle decision p=" << p);
			// q_int = q_bool, q_bool = p q^2 + (1-p) -> q = min(1, (1-p)/p)
			CHECK(close(E.prob_infinite, 1 - std::min(1.0, (1-p)/p), 1e-9), "cycle P(inf) p=" << p << " got " << E.prob_infinite);
		}

		// errors
		Empty4 f; add_rule<int,double>(f, "d", 1.0); add_rule<int>(f, "one", 1.0); // double has no rules
		CHECK(throws([&]{ analyze_tightness(f, f.nt<int>()); }), "reachable nonterminal without rules throws");
		Binary g(1,1,1);
		CHECK(throws([&]{ analyze_tightness(g, (nonterminal_t)3); }), "start out of range throws");
	}

	// ---------------- Monte Carlo via Fleet's generate() ----------------
	{
		// P(infinite) shows up as DepthExceptions at a large depth bound (finite but deep trees are
		// rare for these strongly sub/supercritical choices). Use __generate (no retry).
		for(double q : {0.3, 0.7}) {
			Binary g(q, 0, 1-q);
			g.GRAMMAR_MAX_DEPTH = 300;
			size_t n = 20000, deep = 0;
			for(size_t k=0;k<n;k++) { try { g.__generate(0); } catch(const DepthException&) { deep++; } }
			double pinf = analyze_tightness(g).prob_infinite, f = double(deep)/n, se = std::sqrt(pinf*(1-pinf)/n) + 1e-12;
			std::cout << "MC q=" << q << ": depth-exceeded fraction " << f << " vs P(inf) " << pinf << std::endl;
			CHECK(std::abs(f - pinf) < 5*se + 1e-3, "MC q=" << q);
		}
	}

	// ---------------- random grammars vs independent fixed point ----------------
	{
		auto shapes = all_shapes();
		std::mt19937_64 rng(4242);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		int n_tight = 0, n_not = 0, n_unprod = 0, n_skip = 0;
		for(int trial=0; trial<1000; trial++) {
			Empty4 g;
			int nr = 0;
			// unlike the other tests, terminals are NOT guaranteed: unproductive types occur
			for(int t=0;t<4;t++) if(U(rng) < 0.85)
				for(auto& [ri, args, f] : shapes) if(ri == t and args.empty()) { f(g, "t"+std::to_string(t)+"_"+std::to_string(nr++), 0.1 + 2*U(rng)); break; }
			int extra = 1 + (int)(U(rng)*8);
			for(int k=0;k<extra;k++) {
				auto& [ri, args, f] = shapes[(size_t)(U(rng)*shapes.size()) % shapes.size()];
				f(g, "r"+std::to_string(nr++), U(rng) < 0.1 ? 0.0 : U(rng));
			}
			nonterminal_t start = (nonterminal_t)(trial % 4);
			TightnessResult T;
			try { T = analyze_tightness(g, start); }
			catch(const std::runtime_error&) { n_skip++; continue; } // reachable nonterminal without rules
			if(T.near_critical or std::abs(T.max_component_rho - 1.0) < 1e-3) { n_skip++; continue; } // plain iteration too slow there

			auto ref = reference_q(g, 2000000);
			double pinf_ref = 1.0 - (double)ref[start];
			CHECK(T.infinite_trees_possible == (pinf_ref > 1e-12), "random trial " << trial << " decision " << T.infinite_trees_possible << " ref P(inf)=" << pinf_ref);
			CHECK(close(T.prob_infinite, pinf_ref, 1e-9), "random trial " << trial << " P(inf) " << T.prob_infinite << " vs " << pinf_ref);
			CHECK(close(T.prob_infinite_numerical, pinf_ref, 1e-9), "random trial " << trial << " numerical P(inf)");
			for(size_t i=0;i<4;i++) if(T.reachable[i]) {
				CHECK(close(T.prob_finite_by_nt[i], (double)ref[i], 1e-9), "random trial " << trial << " q_" << i);
				CHECK(T.tight_by_nt[i] == (ref[i] > 1 - 1e-12), "random trial " << trial << " tight_by_nt " << i);
			}
			// finite expected length implies tight
			auto A = analyze(g, start);
			if(A.finite) CHECK(not T.infinite_trees_possible, "analyze finite but not tight, trial " << trial);
			if(T.infinite_trees_possible) CHECK(not A.finite, "not tight but analyze finite, trial " << trial);
			(T.unproductive_reachable ? n_unprod : (T.infinite_trees_possible ? n_not : n_tight))++;
		}
		std::cout << "random: tight=" << n_tight << " supercritical=" << n_not << " unproductive=" << n_unprod << " skipped=" << n_skip << std::endl;
		CHECK(n_tight > 100 and n_not > 30 and n_unprod > 30, "coverage of all three cases");
	}

	std::cout << (failures ? "FAILED " : "ALL PASSED ") << (checks-failures) << "/" << checks << std::endl;
	return failures ? 1 : 0;
}
