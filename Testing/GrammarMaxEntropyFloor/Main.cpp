// Tests for set_max_entropy_probabilities(..., min_probability): max entropy with a probability floor.
//
// Independent checks:
//  (1) eps = 0 is bit-identical to the old behavior; a non-binding floor gives the unfloored answer
//  (2) brute-force grid search over all floored PCFGs on a one-nonterminal grammar
//  (3) closed-form achievable ranges (min_length / max_length), incl. floors that force infinite length
//  (4) achievable ranges vs exhaustive enumeration of all "vertex" PCFGs on random grammars
//  (5) Lagrangian optimality on multi-type grammars: H(q) - λE(q) <= H* - λL for random floored PCFGs q
//      and perturbations of the solution. Together with E* = L this is exactly constrained optimality.
//  (6) length and entropy vs independent long-double recursions; Monte Carlo length via generate()
//  (7) floors respected, normalization, disabled/unreachable rules untouched, strong exception guarantee,
//      history independence, error handling

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
#define CHECK(cond, msg) do { ++checks; if(!(cond)) { ++failures; if(failures < 60) std::cerr << "FAIL [" << __LINE__ << "]: " << msg << std::endl; } } while(0)
static bool close(double a, double b, double rel, double abs_=1e-12) {
	if(std::isinf(a) or std::isinf(b)) return a == b;
	return std::abs(a-b) <= abs_ + rel*std::max(std::abs(a), std::abs(b));
}
template<typename F> static bool throws(F f) { try { f(); } catch(const std::exception&) { return true; } return false; }

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

// ---------- independent references (long double, straight from the rules, no Eigen) ----------
template<typename G, typename CostF>
long double ref_expected(const G& g, nonterminal_t s, CostF cost) {
	constexpr size_t N = G::N_NTs;
	std::vector<long double> x(N,0), nx(N);
	for(int it=0; it<5000000; it++) {
		long double md=0;
		for(size_t i=0;i<N;i++) {
			long double Z=0; for(auto& r : g.rules[i]) Z += r.p;
			if(Z==0) { nx[i]=0; continue; }
			long double v=0;
			for(auto& r : g.rules[i]) { if(r.p==0) continue; long double t = cost(r); for(auto c : r.get_child_types()) t += x[c]; v += (r.p/Z)*t; }
			nx[i]=v; md=std::max(md, std::abs(nx[i]-x[i]));
		}
		x.swap(nx); if(md < 1e-17L*(1+std::abs(x[s]))) break;
	}
	return x[s];
}
template<typename G>
long double ref_entropy(const G& g, nonterminal_t s) {
	constexpr size_t N = G::N_NTs;
	std::vector<long double> h(N,0), nh(N);
	for(int it=0; it<5000000; it++) {
		long double md=0;
		for(size_t i=0;i<N;i++) {
			long double Z=0; for(auto& r : g.rules[i]) Z += r.p;
			if(Z==0) { nh[i]=0; continue; }
			long double v=0;
			for(auto& r : g.rules[i]) { if(r.p==0) continue; long double p=r.p/Z, t=-std::log(p); for(auto c : r.get_child_types()) t += h[c]; v += p*t; }
			nh[i]=v; md=std::max(md, std::abs(nh[i]-h[i]));
		}
		h.swap(nh); if(md < 1e-17L*(1+std::abs(h[s]))) break;
	}
	return h[s];
}

// ---------- grammars ----------
class Four : public Grammar<int,int, int> { // S -> f(S,S) | g(S) | x | y
public:
	Four() {
		add("f(%s,%s)", +[](int a, int b) -> int { return a+b; });
		add("g(%s)",    +[](int a) -> int { return a; });
		add("x",        +[]() -> int { return 1; });
		add("y",        +[]() -> int { return 2; });
	}
};
class Unary : public Grammar<int,int, int> { // S -> g(S) | x
public:
	Unary() { add("g(%s)", +[](int a) -> int { return a; }); add("x", +[]() -> int { return 1; }); }
};
class Bin : public Grammar<int,int, int> {   // S -> f(S,S) | x
public:
	Bin() { add("f(%s,%s)", +[](int a, int b) -> int { return a+b; }); add("x", +[]() -> int { return 1; }); }
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
class Arith : public Grammar<int,int, int,bool> {
public:
	Arith() {
		add("x",          +[]() -> int { return 0; });
		add("0",          +[]() -> int { return 0; });
		add("1",          +[]() -> int { return 1; });
		add("2",          +[]() -> int { return 2; });
		add("succ(%s)",   +[](int a) -> int { return a+1; });
		add("neg(%s)",    +[](int a) -> int { return -a; });
		add("plus(%s,%s)",  +[](int a, int b) -> int { return a+b; });
		add("minus(%s,%s)", +[](int a, int b) -> int { return a-b; });
		add("times(%s,%s)", +[](int a, int b) -> int { return a*b; });
		add("if(%s,%s,%s)", +[](bool c, int a, int b) -> int { return c ? a : b; });
		add("true",         +[]() -> bool { return true; });
		add("not(%s)",      +[](bool a) -> bool { return !a; });
		add("eq(%s,%s)",    +[](int a, int b) -> bool { return a==b; });
		add("lt(%s,%s)",    +[](int a, int b) -> bool { return a<b; });
	}
};
using G4 = Grammar<int,int, int,bool,double,std::string>;
class Empty4 : public G4 { public: Empty4() {} };
template<typename T> T dflt() { return T{}; }
template<typename R, typename... A> void add_rule(G4& g, const std::string& name, double p) {
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

// ---------- checks shared by all solved cases ----------
// Floors, normalization, reachable usable rules only; E and H vs independent references.
template<typename G, typename CostF>
void check_solution(const G& g, nonterminal_t start, const MaxEntResult& res, const std::vector<double>& p_before, CostF cost, double eps, const std::string& label) {
	auto A = analyze(g, start);
	size_t idx = 0;
	for(size_t i=0;i<G::N_NTs;i++) {
		double sum = 0.0;
		for(const auto& r : g.rules[i]) {
			const double before = p_before[idx++];
			if(not A.reachable[i]) { CHECK(r.p == before, label << ": unreachable rule " << r.format << " changed"); continue; }
			if(before == 0.0)      { CHECK(r.p == 0.0, label << ": disabled rule " << r.format << " enabled"); continue; }
			CHECK(r.p >= eps * (1 - 1e-12), label << ": rule " << r.format << " below floor: " << r.p << " < " << eps);
			CHECK(close(r.logp, std::log(r.p), 1e-15, 0), label << ": logp out of sync");
			sum += r.p;
		}
		if(A.reachable[i]) CHECK(close(sum, 1.0, 1e-12) and close(g.Z[i], sum, 1e-15) and close(g.logZ[i], std::log(sum), 1e-12, 1e-15), label << ": normalization / Z cache, nt " << i);
	}
	const double E = (double)ref_expected(g, start, cost);
	CHECK(close(E, res.target_length, 2e-8), label << ": length " << E << " vs target " << res.target_length);
	const double H = (double)ref_entropy(g, start);
	CHECK(close(H, res.entropy, 1e-8, 1e-10), label << ": entropy " << H << " vs reported " << res.entropy);
	CHECK(close(res.log_partition, res.entropy - res.lambda * res.achieved_length, 1e-12, 1e-12), label << ": log_partition = H - λE");
}

// Lagrangian optimality: for floored PCFGs q (random, and perturbations of the solution),
// H(q) - λ·E(q) <= H* - λ·L. Evaluated with the independent references.
template<typename G, typename CostF>
void check_lagrangian(G& g, nonterminal_t start, const MaxEntResult& res, CostF cost, double eps, std::mt19937_64& rng, int n, const std::string& label) {
	const auto A = analyze(g, start);
	std::vector<std::vector<double>> pstar(G::N_NTs);
	for(size_t i=0;i<G::N_NTs;i++) for(const auto& r : g.rules[i]) pstar[i].push_back(r.p);
	const double best = res.entropy - res.lambda * res.target_length;
	std::gamma_distribution<double> Gm(1.0, 1.0);
	std::uniform_real_distribution<double> U(0.0, 1.0);
	int evaluated = 0; double worst = -infinity;
	for(int trial=0; trial<n; trial++) {
		// q = random floored PCFG, or a mixture (1-t)·p* + t·random  (t small: local perturbation)
		const double t = trial % 2 ? 1.0 : std::pow(10.0, -4*U(rng));
		for(size_t i=0;i<G::N_NTs;i++) {
			if(not A.reachable[i]) continue;
			std::vector<double> d; double ds = 0.0; size_t n_us = 0;
			for(size_t k=0;k<g.rules[i].size();k++) { double v = pstar[i][k] > 0 ? Gm(rng) : 0.0; d.push_back(v); ds += v; n_us += pstar[i][k] > 0; }
			for(size_t k=0;k<g.rules[i].size();k++) {
				if(pstar[i][k] == 0) continue;
				const double rnd = eps + (1.0 - n_us*eps) * d[k] / ds;
				g.rules[i][k].p = (1-t)*pstar[i][k] + t*rnd;
			}
		}
		const auto Aq = analyze(g, start, cost);
		if(Aq.finite and Aq.spectral_radius < 0.995) {
			evaluated++;
			const double Eq = (double)ref_expected(g, start, cost), Hq = (double)ref_entropy(g, start);
			const double gap = (Hq - res.lambda*Eq) - best;
			worst = std::max(worst, gap / (1 + std::abs(best)));
			CHECK(gap <= 1e-9 * (1 + std::abs(best) + std::abs(res.lambda*Eq)), label << ": Lagrangian violated by q (t=" << t << "): gap " << gap);
		}
	}
	for(size_t i=0;i<G::N_NTs;i++) for(size_t k=0;k<g.rules[i].size();k++) g.rules[i][k].p = pstar[i][k]; // restore
	CHECK(evaluated >= n/4, label << ": too few finite comparison PCFGs (" << evaluated << ")");
	std::cout << "  " << label << ": Lagrangian check on " << evaluated << " floored PCFGs, worst relative gap " << worst << " (must be <= 0)" << std::endl;
}

// Exhaustive enumeration of vertex PCFGs (eps everywhere, spare mass on one rule per nonterminal):
// smallest and largest expected length. Max/min of E (and of rho) over the floored set are attained at vertices.
template<typename G, typename CostF>
std::pair<double,double> brute_range(G& g, nonterminal_t start, CostF cost, double eps) {
	const auto A = analyze(g, start);
	std::vector<std::vector<double>> pstar(G::N_NTs);
	for(size_t i=0;i<G::N_NTs;i++) for(const auto& r : g.rules[i]) pstar[i].push_back(r.p);
	std::vector<size_t> nts; for(size_t i=0;i<G::N_NTs;i++) if(A.reachable[i]) nts.push_back(i);
	std::vector<std::vector<size_t>> usable(G::N_NTs);
	for(size_t i : nts) for(size_t k=0;k<g.rules[i].size();k++) if(pstar[i][k] > 0) usable[i].push_back(k);
	std::vector<size_t> choice(nts.size(), 0);
	double mn = infinity, mx = -infinity;
	while(true) {
		for(size_t j=0;j<nts.size();j++) {
			const size_t i = nts[j], n = usable[i].size();
			for(size_t k : usable[i]) g.rules[i][k].p = eps;
			g.rules[i][usable[i][choice[j]]].p += 1.0 - n*eps;
		}
		const double E = analyze(g, start, cost).expected_length;
		mn = std::min(mn, E); mx = std::max(mx, E);
		size_t j = 0;
		while(j < nts.size() and ++choice[j] == usable[nts[j]].size()) { choice[j] = 0; j++; }
		if(j == nts.size()) break;
	}
	for(size_t i=0;i<G::N_NTs;i++) for(size_t k=0;k<g.rules[i].size();k++) g.rules[i][k].p = pstar[i][k];
	return {mn, mx};
}

template<typename G> std::vector<double> probs(const G& g) { std::vector<double> v; for(size_t i=0;i<G::N_NTs;i++) for(auto& r : g.rules[i]) v.push_back(r.p); return v; }

int main() {
	std::cout << std::setprecision(12);
	std::mt19937_64 rng(2026);

	// ---------------- (1) eps = 0 unchanged; a non-binding floor changes nothing ----------------
	{
		RR a, b;
		set_max_entropy_probabilities(a, 4.0);
		set_max_entropy_probabilities(b, 4.0, UnitCost{}, 1e-9, 0.0);
		CHECK(same(snapshot(a), snapshot(b)), "explicit min_probability = 0 is bit-identical to the default");

		Four f0, f1;
		set_max_entropy_probabilities(f0, 2.0);                               // smallest p here is 0.143
		auto r1 = set_max_entropy_probabilities(f1, 2.0, UnitCost{}, 1e-9, 0.05);
		double md = 0; auto p0 = probs(f0), p1 = probs(f1);
		for(size_t k=0;k<p0.size();k++) md = std::max(md, std::abs(p0[k]-p1[k]));
		CHECK(md < 1e-9, "non-binding floor gives the unfloored answer, max diff " << md);
		CHECK(r1.rules_at_floor == 0 and r1.min_probability == 0.05, "non-binding floor: rules_at_floor = 0");
	}

	// ---------------- (2) brute force: S -> f(S,S) | g(S) | x | y ----------------
	// Constraint 2 pf + pg = 1 - 1/L; free parameters pf, px (py = rest). Grid with local refinement.
	{
		auto stats = [](double pf, double pg, double px, double py) {
			const double rho = 2*pf + pg;
			if(rho >= 1) return std::pair<double,double>{infinity, -infinity};
			double h = 0; for(double q : {pf,pg,px,py}) if(q > 0) h -= q*std::log(q);
			return std::pair<double,double>{1/(1-rho), h/(1-rho)};
		};
		struct Case { double eps, L; };
		for(Case cs : std::vector<Case>{{0.15,2.0},{0.05,6.0},{0.2,3.0},{0.1,1.6},{0.22,4.0},{0.24,4.0},{0.01,1.05},{0.12,8.0}}) {
			const double eps = cs.eps, L = cs.L, rho = 1 - 1/L;
			double bestH = -infinity, bf=0, bg=0, bx=0, by=0;
			double flo = eps, fhi = rho/2, xlo = 0, xhi = 1;
			for(int level=0; level<6; level++) {             // successively finer grids around the best point
				const int n = 300;
				double nbf = bf, nbx = bx;
				for(int i=0;i<=n;i++) {
					const double pf = flo + (fhi - flo) * i / n, pg = rho - 2*pf;
					if(pf < eps or pg < eps - 1e-15) continue;
					const double rest = 1 - pf - pg;
					for(int j=0;j<=n;j++) {
						const double px = std::max(eps, xlo) + (std::min(rest - eps, xhi) - std::max(eps, xlo)) * j / n, py = rest - px;
						if(px < eps - 1e-15 or py < eps - 1e-15) continue;
						auto [E, H] = stats(pf, pg, px, py);
						if(H > bestH) { bestH = H; nbf = pf; nbx = px; bf = pf; bg = pg; bx = px; by = py; }
					}
				}
				const double wf = (fhi - flo) / 20, wx = 1.0 / std::pow(20.0, level+1);
				flo = std::max(eps, nbf - wf); fhi = std::min(rho/2, nbf + wf); xlo = nbx - wx; xhi = nbx + wx;
			}
			Four g;
			auto res = set_max_entropy_probabilities(g, L, UnitCost{}, 1e-10, eps);
			const double pf = g.get_rule("f(%s,%s)")->p, pg = g.get_rule("g(%s)")->p, px = g.get_rule("x")->p, py = g.get_rule("y")->p;
			std::cout << "  brute eps=" << eps << " L=" << L << ": H*=" << res.entropy << " brute H=" << bestH << "  p*=(" << pf << "," << pg << "," << px << "," << py
			          << ") brute=(" << bf << "," << bg << "," << bx << "," << by << ") at floor: " << res.rules_at_floor << std::endl;
			CHECK(res.entropy >= bestH - 1e-10, "brute eps=" << eps << " L=" << L << ": brute force found higher entropy " << bestH << " > " << res.entropy);
			CHECK(res.entropy - bestH <= 1e-7, "brute eps=" << eps << " L=" << L << ": solution entropy " << res.entropy << " not attained by any grid point (" << bestH << ")");
			CHECK(close(pf, bf, 0, 1e-4) and close(pg, bg, 0, 1e-4) and close(px, bx, 0, 1e-4) and close(py, by, 0, 1e-4), "brute eps=" << eps << " L=" << L << ": probabilities differ from brute force");
			CHECK(px == py, "brute: symmetric terminals get identical probabilities");
			check_solution(g, 0, res, std::vector<double>{1,1,1,1}, UnitCost{}, eps, "brute eps=" + std::to_string(eps));
		}
	}

	// ---------------- (3) closed-form achievable ranges ----------------
	{
		// Four: smallest = spare mass on a terminal: rho = 3 eps, E = 1/(1-3eps).
		//       largest  = spare on f: rho = 2(1-3eps) + eps = 2 - 5eps; infinite iff eps <= 0.2.
		for(double eps : {0.01, 0.1, 0.2, 0.21, 0.22, 0.24}) {
			Four g;
			const double Emin = 1/(1-3*eps), rmax = 2 - 5*eps, Emax = rmax >= 1 ? infinity : 1/(1-rmax);
			const double L = std::isfinite(Emax) ? 0.5*(Emin + Emax) : Emin + 1.0;
			auto res = set_max_entropy_probabilities(g, L, UnitCost{}, 1e-9, eps);
			CHECK(close(res.min_length, Emin, 1e-12), "Four eps=" << eps << " min_length " << res.min_length << " vs " << Emin);
			CHECK(close(res.max_length, Emax, 1e-12), "Four eps=" << eps << " max_length " << res.max_length << " vs " << Emax);
			Four h; auto snap = snapshot(h);
			CHECK(throws([&]{ set_max_entropy_probabilities(h, Emin * (1 - 1e-6), UnitCost{}, 1e-9, eps); }), "Four eps=" << eps << ": target below min_length must throw");
			if(std::isfinite(Emax)) CHECK(throws([&]{ set_max_entropy_probabilities(h, Emax * (1 + 1e-6), UnitCost{}, 1e-9, eps); }), "Four eps=" << eps << ": target above max_length must throw");
			CHECK(same(snap, snapshot(h)), "Four eps=" << eps << ": grammar unchanged after throws");
		}
		// S -> g(S) | x, eps = 0.4: E ranges over [1/(1-0.4), 1/(1-0.6)] = [1.667, 2.5]
		{
			Unary g; auto res = set_max_entropy_probabilities(g, 2.0, UnitCost{}, 1e-9, 0.4);
			CHECK(close(res.min_length, 1/0.6, 1e-12) and close(res.max_length, 2.5, 1e-12), "Unary eps=0.4 range " << res.min_length << " .. " << res.max_length);
			CHECK(close(g.get_rule("g(%s)")->p, 0.5, 1e-9), "Unary: E = 2 needs p(g) = 1/2, got " << g.get_rule("g(%s)")->p);
			Unary h; CHECK(throws([&]{ set_max_entropy_probabilities(h, 3.0, UnitCost{}, 1e-9, 0.4); }), "Unary: target 3 > max 2.5 must throw");
		}
		// S -> f(S,S) | x: eps = 0.5 forces p(f) = 1/2, rho = 1 -> infinite for every target.
		{
			Bin g; auto snap = snapshot(g);
			CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, UnitCost{}, 1e-9, 0.5); }), "Bin eps=0.5: floor forces infinite length");
			CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, UnitCost{}, 1e-9, 0.6); }), "Bin eps=0.6: eps * n > 1");
			CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, UnitCost{}, 1e-9, -0.1); }), "negative floor");
			CHECK(throws([&]{ set_max_entropy_probabilities(g, 5.0, UnitCost{}, 1e-9, std::nan("")); }), "NaN floor");
			CHECK(same(snap, snapshot(g)), "Bin: grammar unchanged after floor errors");
			// eps = 0.45: p(f) >= 0.45 -> E >= 1/(1-0.9) = 10
			Bin h; auto res = set_max_entropy_probabilities(h, 12.0, UnitCost{}, 1e-9, 0.45);
			CHECK(close(res.min_length, 10.0, 1e-10) and std::isinf(res.max_length), "Bin eps=0.45 range " << res.min_length << " .. " << res.max_length);
			// one rule type, so E determines p(f) = (1 - 1/L)/2 regardless of the floor
			CHECK(close(h.get_rule("f(%s,%s)")->p, (1 - 1/12.0)/2, 1e-9), "Bin eps=0.45: p(f) from E");
			Bin k; CHECK(throws([&]{ set_max_entropy_probabilities(k, 9.0, UnitCost{}, 1e-9, 0.45); }), "Bin eps=0.45: target 9 < min 10 must throw");
		}
	}

	// ---------------- (5)+(6) multi-type grammars: Lagrangian optimality, references, Monte Carlo ----------------
	for(double eps : {0.01, 0.03, 0.05}) {
		for(double L : {2.5, 4.0, 10.0}) {
			RR g; auto before = probs(g);
			MaxEntResult res;
			try { res = set_max_entropy_probabilities(g, L, UnitCost{}, 1e-10, eps); }
			catch(const std::invalid_argument& e) { std::cout << "  RR eps=" << eps << " L=" << L << ": refused (" << e.what() << ")" << std::endl; continue; }
			const std::string label = "RR eps=" + std::to_string(eps) + " L=" + std::to_string(L);
			check_solution(g, g.nt<bool>(), res, before, UnitCost{}, eps, label);
			check_lagrangian(g, g.nt<bool>(), res, UnitCost{}, eps, rng, 400, label);
			CHECK(g.get_rule("f0(%s)")->p == g.get_rule("f8(%s)")->p and g.get_rule("and(%s,%s)")->p == g.get_rule("or(%s,%s)")->p, label << ": symmetric rules equal");
		}
	}
	for(double eps : {0.005, 0.02, 0.04}) {
		for(double L : {1.5, 3.0, 8.0}) {
			Arith g; auto before = probs(g);
			MaxEntResult res;
			try { res = set_max_entropy_probabilities(g, L, UnitCost{}, 1e-10, eps); }
			catch(const std::invalid_argument& e) { std::cout << "  Arith eps=" << eps << " L=" << L << ": refused (" << e.what() << ")" << std::endl; continue; }
			const std::string label = "Arith eps=" + std::to_string(eps) + " L=" + std::to_string(L);
			std::cout << "  " << label << ": p(if)=" << g.get_rule("if(%s,%s,%s)")->p << " p(times)=" << g.get_rule("times(%s,%s)")->p << " rules at floor: " << res.rules_at_floor << std::endl;
			check_solution(g, g.nt<int>(), res, before, UnitCost{}, eps, label);
			check_lagrangian(g, g.nt<int>(), res, UnitCost{}, eps, rng, 400, label);
			auto [bmin, bmax] = brute_range(g, g.nt<int>(), UnitCost{}, eps);
			CHECK(close(res.min_length, bmin, 1e-10) and close(res.max_length, bmax, 1e-10), label << ": range " << res.min_length << ".." << res.max_length << " vs brute " << bmin << ".." << bmax);
			for(auto& r : g.rules[0]) CHECK(r.p >= eps*(1-1e-12), label << ": floor");
			if(L <= 3.0) { // Monte Carlo length through Fleet's sampler
				g.GRAMMAR_MAX_DEPTH = 100000;
				double m=0, m2=0; const int n = 100000;
				for(int k=1;k<=n;k++) { double c = (double)g.generate(g.nt<int>()).count(); double d = c-m; m += d/k; m2 += d*(c-m); }
				const double se = std::sqrt(m2/(n-1)/n);
				CHECK(std::abs(m - L) < 5*se, label << ": Monte Carlo length " << m << " +- " << se);
			}
		}
	}
	// non-unit cost with a floor
	{
		auto cost = [](const Rule& r){ return 1.0 + r.N; };
		Arith g; auto before = probs(g);
		auto res = set_max_entropy_probabilities(g, 6.0, g.nt<int>(), cost, 1e-10, 1e-10, 0.02);
		check_solution(g, g.nt<int>(), res, before, cost, 0.02, "Arith cost=1+arity");
		check_lagrangian(g, g.nt<int>(), res, cost, 0.02, rng, 300, "Arith cost=1+arity");
	}

	// ---------------- (7) floor + history independence, idempotence ----------------
	{
		Arith a, b;
		set_max_entropy_probabilities(a, 1.2, UnitCost{}, 1e-10, 0.01);  // something else first
		set_max_entropy_probabilities(a, 5.0, UnitCost{}, 1e-10, 0.03);
		set_max_entropy_probabilities(b, 5.0, UnitCost{}, 1e-10, 0.03);
		double md = 0; auto pa = probs(a), pb = probs(b);
		for(size_t k=0;k<pa.size();k++) md = std::max(md, std::abs(pa[k]-pb[k]));
		CHECK(md < 1e-9, "floored result independent of call history, max diff " << md);
		set_max_entropy_probabilities(b, 5.0, UnitCost{}, 1e-10, 0.03);
		pa = probs(b); md = 0; for(size_t k=0;k<pa.size();k++) md = std::max(md, std::abs(pa[k]-pb[k]));
		CHECK(md < 1e-9, "floored result idempotent, max diff " << md);
	}

	// ---------------- random grammars: everything at once ----------------
	{
		auto shapes = all_shapes();
		std::uniform_real_distribution<double> U(0.0, 1.0);
		int solved = 0, refused = 0, other = 0;
		for(int trial=0; trial<250; trial++) {
			Empty4 g;
			int nr = 0;
			for(int t=0;t<4;t++) for(auto& [ri, args, f] : shapes) if(ri == t and args.empty()) { f(g, "t"+std::to_string(t), 0.1 + 2*U(rng)); break; }
			int extra = 1 + (int)(U(rng)*7);
			for(int k=0;k<extra;k++) { auto& [ri, args, f] = shapes[(size_t)(U(rng)*shapes.size()) % shapes.size()]; f(g, "r"+std::to_string(nr++), U(rng) < 0.1 ? 0.0 : U(rng)); }
			const nonterminal_t s = (nonterminal_t)(trial % 4);
			size_t nmax = 1; for(int i=0;i<4;i++) { size_t n=0; for(auto& r : g.rules[i]) n += r.p > 0; nmax = std::max(nmax, n); }
			const double eps = (0.02 + 0.9*U(rng)) / nmax * (U(rng) < 0.5 ? 0.3 : 1.0);
			const double L = 1.05 + 12*U(rng)*U(rng);
			auto before = probs(g); auto snap = snapshot(g);
			MaxEntResult res;
			try { res = set_max_entropy_probabilities(g, L, s, UnitCost{}, 1e-9, 1e-10, eps); }
			catch(const std::invalid_argument&) {
				refused++;
				CHECK(same(snap, snapshot(g)), "random " << trial << ": grammar changed despite invalid_argument");
				// the refusal must be right: the target is outside the brute-force range of floored PCFGs
				try {
					auto A = analyze(g, s); (void)A;
					bool ok_eps = true;
					for(int i=0;i<4;i++) { size_t n=0; for(auto& r : g.rules[i]) n += r.p > 0; if(A.reachable[i] and n*eps > 1 + 1e-12) ok_eps = false; }
					if(ok_eps) {
						auto [bmin, bmax] = brute_range(g, s, UnitCost{}, eps);
						CHECK(L <= bmin*(1+1e-9) or L >= bmax*(1-1e-9), "random " << trial << ": refused L=" << L << " although brute range is " << bmin << ".." << bmax << " (eps " << eps << ")");
					}
				} catch(const std::exception&) {}
				continue;
			}
			catch(const std::runtime_error& e) { other++; CHECK(same(snap, snapshot(g)), "random " << trial << ": grammar changed despite runtime_error"); std::cout << "  random " << trial << " runtime_error: " << e.what() << std::endl; continue; }
			solved++;
			const std::string label = "random " + std::to_string(trial);
			check_solution(g, s, res, before, UnitCost{}, eps, label);
			// range vs exhaustive vertex enumeration (restore to the pre-call probabilities first: only usable rules matter)
			{
				auto now = probs(g); size_t idx = 0;
				for(int i=0;i<4;i++) for(auto& r : g.rules[i]) r.p = before[idx++];
				auto [bmin, bmax] = brute_range(g, s, UnitCost{}, eps);
				CHECK(close(res.min_length, bmin, 1e-9) and close(res.max_length, bmax, 1e-9), label << ": range " << res.min_length << ".." << res.max_length << " vs brute " << bmin << ".." << bmax);
				idx = 0; for(int i=0;i<4;i++) for(auto& r : g.rules[i]) r.p = now[idx++];
			}
			if(res.spectral_radius < 0.98) check_lagrangian(g, s, res, UnitCost{}, eps, rng, 120, label);
		}
		std::cout << "random: solved=" << solved << " refused=" << refused << " runtime_error=" << other << std::endl;
		CHECK(solved > 100, "enough random cases solved");
	}

	std::cout << (failures ? "FAILED " : "ALL PASSED ") << (checks-failures) << "/" << checks << std::endl;
	return failures ? 1 : 0;
}
