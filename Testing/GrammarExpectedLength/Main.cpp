// Tests for GrammarExpectedLength.h
//  - closed-form cases
//  - independent reference: fixed-point iteration computed directly from the rules (long double)
//  - Monte Carlo using Fleet's own Grammar::generate() and Node::count()/count_terminals()
//  - edge cases: critical/supercritical, unreachable/empty nonterminals, zero-prob rules, errors

#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <functional>
#include <cassert>

#include "EigenLib.h"
#include "Grammar.h"
#include "GrammarExpectedLength.h"

using namespace GrammarExpectedLength;

static int failures = 0;
static int checks   = 0;
#define CHECK(cond, msg) do { ++checks; if(!(cond)) { ++failures; std::cerr << "FAIL [" << __LINE__ << "]: " << msg << std::endl; } } while(0)

static bool close(double a, double b, double rel=1e-10, double abs_=1e-12) {
	if(std::isinf(a) or std::isinf(b)) return a == b;
	return std::abs(a-b) <= abs_ + rel*std::max(std::abs(a), std::abs(b));
}

template<typename F> static bool throws(F f) { try { f(); } catch(const std::exception&) { return true; } return false; }

// Independent reference: x <- c + M x by direct iteration over the rules (no matrices, no Eigen).
template<typename G, typename CostF>
std::vector<long double> reference_fixed_point(const G& g, CostF cost, size_t iters=200000) {
	constexpr size_t N = G::N_NTs;
	std::vector<long double> x(N, 0.0L), nx(N);
	for(size_t it=0; it<iters; it++) {
		long double maxdiff = 0;
		for(size_t i=0;i<N;i++) {
			long double Z = 0; for(const auto& r : g.rules[i]) Z += r.p;
			if(Z == 0) { nx[i] = 0; continue; }
			long double v = 0;
			for(const auto& r : g.rules[i]) {
				long double s = cost(r);
				for(auto t : r.get_child_types()) s += x[t];
				v += (r.p / Z) * s;
			}
			nx[i] = v;
			maxdiff = std::max(maxdiff, std::abs(nx[i]-x[i]));
		}
		x.swap(nx);
		if(maxdiff < 1e-15L * (1 + std::abs(x[0]))) break;
	}
	return x;
}

// Monte Carlo with Fleet's sampler. Returns (mean, standard error).
template<typename G, typename Stat>
std::pair<double,double> monte_carlo(G& g, nonterminal_t start, size_t n, Stat stat) {
	double m=0, m2=0;
	for(size_t k=1;k<=n;k++) {
		Node t = g.generate(start);
		double v = stat(t);
		double d = v - m; m += d/k; m2 += d*(v-m);
	}
	return {m, std::sqrt(m2/(n-1)/n)};
}

// ---------------------------------------------------------------------------------------
// Grammars
// ---------------------------------------------------------------------------------------

// S -> f(S,S) [a] | g(S) [b] | x [c]   (single nonterminal). rho = (2a+b)/(a+b+c), E = 1/(1-rho)
class Binary : public Grammar<int,int, int> {
public:
	Binary(double a, double b, double c) {
		add("f(%s,%s)", +[](int x, int y) -> int { return x+y; }, a);
		add("g(%s)",    +[](int x) -> int { return x; }, b);
		add("x",        +[]() -> int { return 1; }, c);
	}
};

// RationalRules-shaped grammar: 9 features (bool <- obj), and/or (bool<-bool,bool), not, x:obj
// Hand computation: E_obj = 1; E_bool = 1 + (5/12)E_bool + (9/12)*1  =>  E_bool = 3.
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

// Four types for random grammars and edge cases.
using G4 = Grammar<int,int, int,bool,double,std::string>;
class Empty4 : public G4 { public: Empty4() {} };

// Add a rule to G4 with return type R and argument types Args... by type index at runtime.
// (Fleet's add() is typed at compile time, so we enumerate the shapes we need.)
template<typename T> T dflt() { return T{}; }

template<typename R, typename... A>
void add_rule(G4& g, const std::string& name, double p) {
	std::string fmt = name;
	if constexpr (sizeof...(A) > 0) {
		fmt += "(";
		for(size_t i=0;i<sizeof...(A);i++) fmt += (i ? ",%s" : "%s");
		fmt += ")";
	}
	g.add(fmt, std::function<R(A...)>([](A...) -> R { return dflt<R>(); }), p);
}

using AddF = std::function<void(G4&, const std::string&, double)>;

// all rules with arity 0..2 over the 4 types (4 * (1 + 4 + 16) = 84 shapes)
std::vector<std::tuple<int,std::vector<int>,AddF>> all_shapes() {
	std::vector<std::tuple<int,std::vector<int>,AddF>> v;
	auto per_ret = [&]<typename R>(int ri) {
		v.push_back({ri, {}, [](G4& g, const std::string& n, double p){ add_rule<R>(g,n,p); }});
		auto one = [&]<typename A>(int ai) {
			v.push_back({ri, {ai}, [](G4& g, const std::string& n, double p){ add_rule<R,A>(g,n,p); }});
			auto two = [&]<typename B>(int bi) {
				v.push_back({ri, {ai,bi}, [](G4& g, const std::string& n, double p){ add_rule<R,A,B>(g,n,p); }});
			};
			two.template operator()<int>(0); two.template operator()<bool>(1); two.template operator()<double>(2); two.template operator()<std::string>(3);
		};
		one.template operator()<int>(0); one.template operator()<bool>(1); one.template operator()<double>(2); one.template operator()<std::string>(3);
	};
	per_ret.template operator()<int>(0); per_ret.template operator()<bool>(1); per_ret.template operator()<double>(2); per_ret.template operator()<std::string>(3);
	return v;
}

int main() {
	std::cout << std::setprecision(12);

	// ---------------- closed form: single nonterminal ----------------
	{
		struct Case { double a,b,c; };
		for(Case cs : std::vector<Case>{{1,0,4},{3,0,7},{1,1,2},{2,5,3},{0,0,1},{0.0001,0.3,7},{4.5,0,5.5}}) {
			Binary g(cs.a,cs.b,cs.c);
			double Z = cs.a+cs.b+cs.c, rho = (2*cs.a+cs.b)/Z;
			auto R = analyze(g);
			CHECK(R.finite, "binary finite a=" << cs.a << " b=" << cs.b << " c=" << cs.c << " rho=" << R.spectral_radius);
			CHECK(close(R.spectral_radius, rho, 1e-12, 1e-14), "binary rho " << R.spectral_radius << " vs " << rho);
			CHECK(close(R.expected_length, 1.0/(1.0-rho)), "binary E " << R.expected_length << " vs " << 1/(1-rho));
			// leaves: L = 1 + 2a/Z... -> L = c/Z / (1-rho)
			CHECK(close(analyze(g, TerminalCost{}).expected_length, (cs.c/Z)/(1-rho)), "binary leaves");
			CHECK(close(analyze(g, InternalCost{}).expected_length + analyze(g, TerminalCost{}).expected_length, R.expected_length), "internal+terminal=total");
			CHECK(close(expected_length(g), R.expected_length, 0, 0), "convenience wrapper");
			CHECK(close(expected_length(g, (nonterminal_t)0), R.expected_length, 0, 0), "convenience wrapper with start");
		}
		// critical (rho == 1 exactly) and supercritical
		for(auto [a,c] : std::vector<std::pair<double,double>>{{1,1},{0.5,0.5},{3,3},{2,1},{10,1}}) {
			Binary g(a,0,c);
			auto R = analyze(g);
			CHECK(not R.finite and std::isinf(R.expected_length), "critical/supercritical infinite a=" << a << " c=" << c);
			CHECK(close(R.spectral_radius, 2*a/(a+c), 1e-12), "critical rho");
		}
		// near critical but resolvable: rho = 1 - 1e-6 -> E = 1e6
		{
			double eps = 1e-6; // 2a/(a+c) = 1-eps
			double a = 1.0, c = 2*a/(1-eps) - a;
			Binary g(a,0,c);
			auto R = analyze(g);
			CHECK(R.finite and close(R.expected_length, 1.0/(1.0-2*a/(a+c)), 1e-6), "near critical E " << R.expected_length);
		}
		// integer start must pick the start overload, not be treated as a cost functor
		{
			Binary g(1,1,4);
			CHECK(close(analyze(g, 0).expected_length, analyze(g).expected_length, 0, 0), "analyze(g, 0) overload");
		}
	}

	// ---------------- RationalRules-shaped grammar ----------------
	{
		RR g;
		auto R = analyze(g);
		CHECK(R.finite and close(R.expected_length, 3.0), "RR E_bool = 3, got " << R.expected_length);
		CHECK(close(R.expected_length_by_nt[g.nt<double>()], 1.0), "RR E_obj = 1");
		CHECK(close(R.spectral_radius, 5.0/12.0, 1e-12), "RR rho = 5/12");
		CHECK(close(expected_length(g, g.nt<double>()), 1.0), "RR from obj");

		g.GRAMMAR_MAX_DEPTH = 5000;
		auto [m, se] = monte_carlo(g, g.nt<bool>(), 400000, [](const Node& n){ return (double)n.count(); });
		std::cout << "RR MC mean=" << m << " se=" << se << " exact=3" << std::endl;
		CHECK(std::abs(m - 3.0) < 5*se, "RR Monte Carlo");

		// change_probability path
		g.change_probability("and(%s,%s)", 4.0); // now bool Z=15; M_bb = (8+2+1)/15 = 11/15; obj 9/15
		// E = (1 + 9/15) / (1 - 11/15) = (24/15)/(4/15) = 6
		CHECK(close(analyze(g).expected_length, 6.0), "RR after change_probability: " << analyze(g).expected_length);
	}

	// ---------------- edge cases ----------------
	{
		// unreachable supercritical + unreachable empty nonterminals must not matter
		Empty4 g;
		add_rule<int,int,int>(g, "plus", 1.0);
		add_rule<int>(g, "one", 3.0);
		add_rule<bool,bool,bool>(g, "and", 10.0);   // supercritical but unreachable from int
		add_rule<bool>(g, "T", 1.0);
		// double: no rules at all (unreachable); string: no rules
		auto R = analyze(g, g.nt<int>());
		CHECK(R.finite and close(R.expected_length, 1.0/(1.0-0.5)), "unreachable parts ignored, E=" << R.expected_length);
		CHECK(R.reachable[g.nt<int>()] and not R.reachable[g.nt<bool>()] and not R.reachable[g.nt<double>()], "reachable flags");
		CHECK(std::isnan(R.expected_length_by_nt[g.nt<bool>()]), "unreachable -> NaN");
		CHECK(std::isinf(analyze(g, g.nt<bool>()).expected_length), "from bool supercritical");
		CHECK(throws([&]{ analyze(g, g.nt<double>()); }), "start with no rules throws");

		// zero-probability rule into an empty nonterminal is never sampled -> ignored
		add_rule<int,double>(g, "zero", 0.0);
		CHECK(close(analyze(g, g.nt<int>()).expected_length, 2.0), "zero-prob rule ignored");

		// positive-probability rule into empty nonterminal -> throws (generate would fail)
		add_rule<int,std::string>(g, "bad", 0.1);
		CHECK(throws([&]{ analyze(g, g.nt<int>()); }), "reachable empty nonterminal throws");

		// out of range start, bad tolerance, bad cost
		Binary b(1,1,4);
		CHECK(throws([&]{ analyze(b, (nonterminal_t)5); }), "start out of range");
		CHECK(throws([&]{ analyze(b, (nonterminal_t)0, UnitCost{}, 1.5); }), "bad tolerance");
		CHECK(throws([&]{ analyze(b, [](const Rule&){ return -1.0; }); }), "negative cost");
		CHECK(throws([&]{ analyze(b, [](const Rule&){ return std::nan(""); }); }), "nan cost");

		// mutually recursive cycle with no direct terminal escape from one type
		Empty4 h;
		add_rule<int,bool>(h, "i_of_b", 1.0);                 // int -> bool
		add_rule<bool,int,int>(h, "b_of_ii", 1.0);            // bool -> int int   (p .4 of bool)
		add_rule<bool>(h, "T", 1.5);                          // bool -> T         (p .6)
		// E_int = 1 + E_bool ; E_bool = 1 + 0.4*2*E_int  => E_bool = 1 + 0.8(1+E_bool) => E_bool = 9, E_int = 10
		auto H = analyze(h, h.nt<int>());
		CHECK(H.finite and close(H.expected_length, 10.0) and close(H.expected_length_by_nt[h.nt<bool>()], 9.0), "cycle E_int=10 got " << H.expected_length);
		CHECK(close(H.spectral_radius, std::sqrt(0.8), 1e-12), "cycle rho = sqrt(0.8), got " << H.spectral_radius);
	}

	// ---------------- random grammars vs reference fixed point and Monte Carlo ----------------
	{
		auto shapes = all_shapes();
		std::mt19937_64 rng(12345);
		std::uniform_real_distribution<double> U(0.0, 1.0);
		int n_finite = 0, n_inf = 0, n_mc = 0;

		for(int trial=0; trial<400; trial++) {
			Empty4 g;
			int nr = 0;
			// every type gets at least one terminal so all are expandable
			for(int t=0;t<4;t++) {
				for(auto& [ri, args, f] : shapes) {
					if(ri == t and args.empty()) { f(g, "t"+std::to_string(t)+"_"+std::to_string(nr++), 0.2 + 3*U(rng)); break; }
				}
			}
			// random extra rules
			int extra = 2 + (int)(U(rng)*10);
			for(int k=0;k<extra;k++) {
				auto& [ri, args, f] = shapes[(size_t)(U(rng)*shapes.size()) % shapes.size()];
				double p = (U(rng) < 0.1 ? 0.0 : U(rng) * (trial % 3 == 0 ? 3.0 : 1.0));
				f(g, "r"+std::to_string(nr++), p);
			}

			nonterminal_t start = (nonterminal_t)(trial % 4);
			auto R = analyze(g, start);

			if(R.finite) {
				n_finite++;
				if(R.spectral_radius < 0.97) {
					auto ref = reference_fixed_point(g, UnitCost{});
					for(size_t i=0;i<4;i++) if(R.reachable[i])
						CHECK(close(R.expected_length_by_nt[i], (double)ref[i], 1e-9), "random ref trial " << trial << " nt " << i << ": " << R.expected_length_by_nt[i] << " vs " << (double)ref[i]);
					auto refT = reference_fixed_point(g, TerminalCost{});
					CHECK(close(analyze(g, start, TerminalCost{}).expected_length, (double)refT[start], 1e-9), "random ref terminals trial " << trial);
				}
				if(R.spectral_radius < 0.75 and n_mc < 25) {
					n_mc++;
					g.GRAMMAR_MAX_DEPTH = 4000;
					auto [m, se] = monte_carlo(g, start, 100000, [](const Node& n){ return (double)n.count(); });
					std::cout << "  MC trial " << trial << " rho=" << R.spectral_radius << " exact=" << R.expected_length << " mc=" << m << " +- " << se << std::endl;
					CHECK(std::abs(m - R.expected_length) < 5*se + 1e-12, "random MC trial " << trial << " mean " << m << " se " << se << " exact " << R.expected_length);
					auto [mt, set] = monte_carlo(g, start, 100000, [](const Node& n){ return (double)n.count_terminals(); });
					double et = analyze(g, start, TerminalCost{}).expected_length;
					CHECK(std::abs(mt - et) < 5*set + 1e-12, "random MC terminals trial " << trial << " mean " << mt << " exact " << et);
				}
			}
			else {
				n_inf++;
				CHECK(R.spectral_radius >= 1.0 - 1e-10, "infinite implies rho>=1");
				// supercritical check: reference fixed point should blow up
				if(R.spectral_radius > 1.01) {
					auto ref = reference_fixed_point(g, UnitCost{}, 5000);
					CHECK(ref[start] > 1e10, "random supercritical diverges trial " << trial << " ref " << (double)ref[start]);
				}
			}
		}
		std::cout << "random grammars: finite=" << n_finite << " infinite=" << n_inf << " monte-carlo=" << n_mc << std::endl;
	}

	std::cout << (failures ? "FAILED " : "ALL PASSED ") << (checks-failures) << "/" << checks << std::endl;
	return failures ? 1 : 0;
}
