// TestTopN: TopN stores exactly the N best distinct hypotheses, accurately.
//
// The reference is a brute-force computation: collect every distinct hypothesis (by value), order
// them exactly as Bayesable::operator<=> does (posterior, then prior, then hash), and take the N
// largest. TopN must agree with it as a SET (no missing, no extra, no duplicates) for many N,
// including N=0, N=1, N larger than the number of distinct values, and boundaries that fall on
// ties. Also: best/worst/sorted, add()'s return value, rejection of -inf/NaN posteriors, copy /
// move / assignment, merging, Z(), compute_posterior, serialization, pop, concurrent adds, and
// TopN fed from an MCMC chain.

#include <algorithm>
#include <random>
#include <set>

#include "TestModels.h"
#include "TopN.h"

using namespace TestHarness;

using H = FiniteHypothesis;

// the reference order: exactly Bayesable::operator<=>, written out independently
bool ref_less(const H& a, const H& b) {
	auto ord = [](double x, double y) -> int { // NaN sorts as the smallest, matching the current fp_ordering
		const bool nx = std::isnan(x), ny = std::isnan(y);
		if(nx and ny) return 0; if(nx) return -1; if(ny) return 1;
		return (x < y) ? -1 : (x > y ? 1 : 0);
	};
	if(int c = ord(a.posterior, b.posterior)) return c < 0;
	if(int c = ord(a.prior, b.prior)) return c < 0;
	return a.hash() < b.hash();
}

// distinct hypotheses (by value), sorted ascending by the reference order
std::vector<H> ref_sorted(const std::vector<H>& xs) {
	std::map<std::string, H> uniq;
	for(auto& h : xs) {
		if(std::isnan(h.posterior) or h.posterior == -infinity) continue; // TopN documents it never stores these
		uniq.emplace(key(h.get_value()), h);
	}
	std::vector<H> v; for(auto& [k,h] : uniq) v.push_back(h);
	std::sort(v.begin(), v.end(), ref_less);
	return v;
}

std::set<std::string> keys_of(const TopN<H>& t) {
	std::set<std::string> s; for(auto& h : t.values()) s.insert(key(h.get_value())); return s;
}
std::set<std::string> top_keys(const std::vector<H>& sorted_asc, size_t N) {
	std::set<std::string> s;
	for(size_t i=0;i<std::min(N, sorted_asc.size());i++) s.insert(key(sorted_asc[sorted_asc.size()-1-i].get_value()));
	return s;
}

// check t holds exactly the top N of xs
bool check_top(const TopN<H>& t, const std::vector<H>& xs, size_t N, const std::string& label) {
	auto ref = ref_sorted(xs);
	auto want = top_keys(ref, N);
	auto got = keys_of(t);
	bool ok = true;
	if(got != want) {
		ok = false;
		std::string ex;
		for(auto& k : want) if(not got.count(k)) { ex = "missing " + k; break; }
		if(ex.empty()) for(auto& k : got) if(not want.count(k)) { ex = "extra " + k; break; }
		CHECK(false, label << ": TopN(" << N << ") holds " << got.size() << " values, expected " << want.size() << "; " << ex);
	}
	if(t.size() != got.size()) { ok = false; CHECK(false, label << ": duplicate values stored"); }
	if(ok and not t.empty()) {
		const auto& best = ref.back();
		const auto& worst = ref[ref.size() - std::min(N, ref.size())];
		if(key(t.best().get_value()) != key(best.get_value()) or key(t.worst().get_value()) != key(worst.get_value())) {
			ok = false; CHECK(false, label << ": best()/worst()");
		}
		// stored hypotheses keep their exact scores
		for(auto& h : t.values()) {
			const auto& r = *std::find_if(ref.begin(), ref.end(), [&](const H& x){ return key(x.get_value()) == key(h.get_value()); });
			if(h.posterior != r.posterior or h.prior != r.prior or h.likelihood != r.likelihood) { ok = false; CHECK(false, label << ": stored scores changed"); break; }
		}
		auto inc = t.sorted(true), dec = t.sorted(false);
		bool sorted_ok = inc.size() == t.size() and dec.size() == t.size();
		for(size_t i=1;i<inc.size();i++) sorted_ok = sorted_ok and ref_less(inc[i-1], inc[i]);
		for(size_t i=0;i<inc.size();i++) sorted_ok = sorted_ok and key(inc[i].get_value()) == key(dec[dec.size()-1-i].get_value());
		if(not sorted_ok) { ok = false; CHECK(false, label << ": sorted()"); }
	}
	CHECK(ok, label << ": TopN(" << N << ") contents");
	return ok;
}

std::vector<H> random_hypotheses(size_t n, std::vector<FiniteHypothesis::datum_t>& data) {
	std::vector<H> v;
	for(size_t i=0;i<n;i++) { H h(finite_grammar.generate()); h.compute_posterior(data); v.push_back(h); }
	return v;
}

int main() {
	test_name = "TopN";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());
	auto states = finite_states();

	// ==================================================================================
	section("preconditions: all 309 programs have distinct hashes (TopN treats equal hashes as equal values)");
	{
		std::set<size_t> hs; for(auto& [k,s] : states) hs.insert(s.node.hash());
		CHECK(hs.size() == states.size(), "hash collisions among the " << states.size() << " programs: " << states.size()-hs.size());
		// natural ties: plus(1,2) and plus(2,1) are different programs with identical prior and posterior
		H a(finite_grammar.simple_parse("plus(1,2)")), b(finite_grammar.simple_parse("plus(2,1)"));
		a.compute_posterior(finite_data); b.compute_posterior(finite_data);
		CHECK(a.posterior == b.posterior and a.prior == b.prior and not (a == b), "test has exact ties between distinct values");
		TopN<H> t(10); t << a; t << b;
		CHECK(t.size() == 2, "two distinct values with tied scores are both stored");
	}

	// ==================================================================================
	section("streams of random hypotheses (many duplicates and ties)");
	for(int rep=0; rep<40; rep++) {
		auto xs = random_hypotheses(500 + myrandom(3000), finite_data);
		for(size_t N : {0ul, 1ul, 2ul, 5ul, 17ul, 50ul, 120ul, 1000ul}) {
			TopN<H> t(N);
			for(auto& h : xs) t << h;
			check_top(t, xs, N, "stream rep " + str(rep));
		}
		// every boundary position: N = each possible size (catches tie handling at the cut)
		if(rep < 3) {
			const size_t ndistinct = ref_sorted(xs).size();
			for(size_t N=1; N<=ndistinct+1; N++) { TopN<H> t(N); for(auto& h : xs) t << h; check_top(t, xs, N, "boundary N=" + str(N)); }
		}
	}

	// ==================================================================================
	section("add() return value, contains, rejected scores");
	{
		auto xs = random_hypotheses(3000, finite_data);
		TopN<H> t(20);
		std::vector<H> seen;
		bool ret_ok = true;
		for(auto& h : xs) {
			auto before = keys_of(t);
			const bool added = t.add(h);
			auto after = keys_of(t);
			ret_ok = ret_ok and (added == (before != after)) and (added == (after.count(key(h.get_value())) and not before.count(key(h.get_value()))));
			seen.push_back(h);
			if(not t.contains(h) and after.count(key(h.get_value()))) ret_ok = false;
		}
		CHECK(ret_ok, "add() returns true exactly when the value was newly stored; contains() agrees");
		H bad(finite_grammar.generate()); bad.compute_posterior(finite_data);
		H neg = bad; neg.posterior = -infinity;
		H nan = bad; nan.posterior = NaN;
		TopN<H> u(5);
		CHECK(not u.add(neg) and not u.add(nan) and u.empty(), "-inf and NaN posteriors are never stored");
		TopN<H> z(0);
		CHECK(not z.add(bad) and z.empty(), "N=0 stores nothing");
	}

	// ==================================================================================
	section("copy, move, assignment, merging, pop, Best");
	{
		auto xs = random_hypotheses(4000, finite_data);
		TopN<H> t(30); for(auto& h : xs) t << h;
		const auto k = keys_of(t);
		TopN<H> c(t);
		CHECK(keys_of(c) == k and c.N == t.N, "copy constructor");
		TopN<H> a(5); a = t;
		CHECK(keys_of(a) == k and a.N == 30, "copy assignment (takes the source's N)");
		a = a;
		CHECK(keys_of(a) == k, "self assignment");
		TopN<H> m(std::move(c));
		CHECK(keys_of(m) == k and m.N == 30, "move constructor");
		TopN<H> ma(7); ma = std::move(m);
		CHECK(keys_of(ma) == k and ma.N == 30, "move assignment");
		// adding more to a copy does not affect the original
		TopN<H> c2(t);
		for(auto& h : random_hypotheses(2000, finite_data)) c2 << h;
		CHECK(keys_of(t) == k, "copies are independent");
		// merge: the top N of the union
		auto ys = random_hypotheses(4000, finite_data);
		TopN<H> t1(25), t2(25);
		for(auto& h : xs) t1 << h;
		for(auto& h : ys) t2 << h;
		t1 << t2;
		std::vector<H> all = xs; all.insert(all.end(), ys.begin(), ys.end());
		check_top(t1, all, 25, "merged");
		std::set<H> st; st << t2;
		CHECK(st.size() == t2.size(), "operator<<(std::set, TopN)");
		// pop removes the best
		TopN<H> p(t);
		const std::string best = key(p.best().get_value());
		p.pop();
		CHECK(p.size() == t.size()-1 and not keys_of(p).count(best), "pop() removes the best");
		Best<H> bst; for(auto& h : xs) bst << h;
		CHECK(key(((H)bst).get_value()) == key(t.best().get_value()) and bst.size() == 1, "Best<T>");
		TopN<H> cl(t); cl.clear();
		CHECK(cl.empty() and cl.N == 30, "clear()");
	}

	// ==================================================================================
	section("Z(), compute_posterior, serialization");
	{
		auto xs = random_hypotheses(5000, finite_data);
		TopN<H> t(200); for(auto& h : xs) t << h;
		for(double T : {1.0, 3.0}) {
			long double m = -INFINITY; for(auto& h : t.values()) m = std::max<long double>(m, h.posterior/T);
			long double s = 0; for(auto& h : t.values()) s += expl(h.posterior/T - m);
			CHECK_CLOSE(t.Z(T), (double)(m + logl(s)), 1e-6, 1e-6, "Z(" << T << ") equals logsumexp of stored posteriors / T (logplusexp is accurate to ~1e-6)");
		}
		// re-score under different data: a NEW TopN holding the stored values ranked by the new posterior
		std::vector<FiniteHypothesis::datum_t> data2 = { FiniteHypothesis::datum_t((short)1, 1), FiniteHypothesis::datum_t((short)2, 7) };
		const auto keys_before = keys_of(t);
		const double best_before = t.best().posterior;
		TopN<H> r = t.compute_posterior(data2);
		std::vector<H> rescored; for(auto h : t.values()) { h.compute_posterior(data2); rescored.push_back(h); }
		check_top(r, rescored, 200, "compute_posterior");
		CHECK(keys_of(t) == keys_before and t.best().posterior == best_before, "compute_posterior leaves the original untouched");
		// serialization round trip (exact doubles)
		const std::string s = t.serialize();
		TopN<H> d = TopN<H>::deserialize(s);
		bool same = d.size() == t.size() and d.N == t.N;
		for(auto& h : t.values()) {
			bool found = false;
			for(auto& g : d.values()) if(key(g.get_value()) == key(h.get_value())) { found = g.posterior == h.posterior and g.prior == h.prior and g.likelihood == h.likelihood; break; }
			same = same and found;
		}
		CHECK(same, "serialize/deserialize round trip preserves values, N and exact scores");
		TopN<H> small(3); for(auto& h : xs) small << h;
		TopN<H> ds = TopN<H>::deserialize(small.serialize());
		CHECK(ds.N == 3 and keys_of(ds) == keys_of(small), "deserialize with N smaller than the default ntop");
	}

	// ==================================================================================
	section("concurrent adds");
	{
		for(int rep=0;rep<10;rep++) {
			auto xs = random_hypotheses(20000, finite_data);
			TopN<H> serial(40); for(auto& h : xs) serial << h;
			TopN<H> conc(40);
			std::vector<std::thread> th;
			for(int k=0;k<4;k++) th.emplace_back([&,k]{ for(size_t i=k;i<xs.size();i+=4) conc << xs[i]; });
			for(auto& x : th) x.join();
			CHECK(keys_of(conc) == keys_of(serial) and conc.size() == 40, "concurrent adds give the serial result (rep " << rep << ")");
		}
	}

	// ==================================================================================
	section("TopN of an MCMC chain's samples");
	{
		std::vector<H> samples;
		TopN<H> t(15);
		MCMCChain<H> chain(H(finite_grammar.generate()), finite_data);
		for(auto& h : chain.run(Control(30000, 0, 1, 0))) { t << h; samples.push_back(h); }
		check_top(t, samples, 15, "MCMC samples");
		// with enough samples the chain finds the true MAP program
		// (the MAP can be tied, e.g. plus(1,sx) and plus(sx,1); TopN then prefers the larger hash)
		double best = -INFINITY;
		for(auto& [k,s] : states) best = std::max(best, s.prior + s.loglik);
		std::set<std::string> maps; for(auto& [k,s] : states) if(s.prior + s.loglik == best) maps.insert(k);
		CHECK(maps.count(key(t.best().get_value())) and close(t.best().posterior, best, 1e-12, 1e-12),
		      "TopN's best is an exact MAP program (" << maps.size() << " tied)");
	}

	return finish();
}
