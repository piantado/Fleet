/*
 * Tests for AStarSearch against an INDEPENDENT brute force: all complete programs with -log prior <= C are built
 * directly from the grammar's rules and scored with the same hypothesis class. A* (with min_score = -C, so it can only
 * report programs inside the brute-force set) must report exactly the brute-force programs with score >= -C, in
 * non-increasing score order. Repeated with likelihood temperatures, a tighter max_loglik, an admissible partial
 * likelihood bound, a tiny frontier (checked up to exact_score_bound()), stopping/resuming, and inadmissible bounds
 * (which must throw).
 */

#include <map>
#include <set>
#include <string>
#include <cmath>

#include "Grammar.h"
#include "Singleton.h"
#include "Builtins.h"
#include "DeterministicLOTHypothesis.h"
#include "AStarSearch.h"
#include "Fleet.h"

size_t nfail = 0, nchecks = 0;
#define CHECK(cond, ...) do { nchecks++; if(not (cond)) { nfail++; print("FAIL:", #cond, __VA_ARGS__); } } while(0)

class MyGrammar : public Grammar<int,int, int,bool>, public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("x",            Builtins::X<MyGrammar>, 2.0);
		add_terminal("1", 1, 1.0);
		add_terminal("2", 2, 0.5);
		add("(%s+%s)",      +[](int a, int b) -> int { return a+b; }, 1.0);
		add("(%s*%s)",      +[](int a, int b) -> int { return a*b; }, 0.7);
		add("(%s-%s)",      +[](int a, int b) -> int { return a-b; }, 0.6);
		add("if(%s,%s,%s)", Builtins::If<MyGrammar,int>, 0.2);
		add("lt(%s,%s)",    +[](int a, int b) -> bool { return a<b; }, 1.0);
		add("not(%s)",      Builtins::Not<MyGrammar>, 0.3);
		add_terminal("T",   true, 0.4);
	}
} grammar;

const double ALPHA = 0.8, NOUT = 50;
class MyHypothesis : public DeterministicLOTHypothesis<MyHypothesis, int, int, MyGrammar, &grammar> {
public:
	using Super = DeterministicLOTHypothesis<MyHypothesis, int, int, MyGrammar, &grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		return std::log(ALPHA*(this->call(d.input, -999) == d.output) + (1-ALPHA)/NOUT);
	}
};

// ---- independent brute force: all complete trees of type nt with -log prior <= budget
std::vector<Node> all_trees(nonterminal_t nt, double budget) {
	std::vector<Node> out;
	for(const Rule& r : grammar.rules[nt]) {
		const double c = -(r.logp - grammar.logZ[nt]);
		if(c > budget + 1e-12) continue;
		std::vector<Node> partial = { grammar.makeNode(&r) };
		std::vector<double> spent = { c };
		for(size_t k = 0; k < r.N; k++) {
			std::vector<Node> np; std::vector<double> ns;
			for(size_t j = 0; j < partial.size(); j++)
				for(const Node& ch : all_trees(r.type(k), budget - spent[j])) {
					Node n = partial[j]; n.set_child(k, Node(ch));
					np.push_back(std::move(n)); ns.push_back(spent[j] + (-grammar.log_probability(ch)));
				}
			partial.swap(np); spent.swap(ns);
		}
		for(auto& n : partial) out.push_back(std::move(n));
	}
	return out;
}

struct Item { double score; std::string s; };

std::vector<Item> brute(double C, double T, std::vector<MyHypothesis::datum_t>& data) {
	std::vector<Item> v;
	for(const Node& n : all_trees(grammar.nt<int>(), C)) {
		MyHypothesis h{Node(n)};
		h.compute_posterior(data);
		double s = h.prior + h.likelihood / T;
		if(s >= -C) v.push_back({s, h.string()});
	}
	std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.score > b.score; });
	return v;
}

// compare A* output to brute force; if bound is finite, only programs with score > bound are guaranteed
void compare(const std::vector<Item>& got, const std::vector<Item>& want, double bound, const std::string& label) {
	for(size_t i = 1; i < got.size(); i++) CHECK(got[i].score <= got[i-1].score + 1e-9, label, "order", i);
	std::multiset<std::string> g, w;
	for(auto& x : got)  if(x.score > bound + 1e-9) g.insert(x.s);
	for(auto& x : want) if(x.score > bound + 1e-9) w.insert(x.s);
	CHECK(g == w, label, "set mismatch", g.size(), w.size());
	// position-wise scores must agree on the guaranteed prefix
	size_t n = 0; while(n < want.size() and want[n].score > bound + 1e-9) n++;
	CHECK(got.size() >= n, label, "too few", got.size(), n);
	for(size_t i = 0; i < std::min(n, got.size()); i++)
		CHECK(std::abs(got[i].score - want[i].score) < 1e-9, label, "score at", i, got[i].s, want[i].s);
	std::set<std::string> uniq; for(auto& x : got) uniq.insert(x.s);
	CHECK(uniq.size() == got.size(), label, "duplicate program reported");
}

using AS = AStarSearch<MyHypothesis>;

std::vector<Item> run_astar(AS& a, AS::Status* st = nullptr, size_t stop_after = SIZE_MAX) {
	std::vector<Item> got;
	auto s = a.run([&](MyHypothesis& h, double score) { got.push_back({score, h.string()}); return got.size() < stop_after; });
	if(st) *st = s;
	return got;
}

int main(int argc, char** argv) {
	Fleet fleet("AStarSearch tests");
	fleet.initialize(argc, argv);

	std::vector<MyHypothesis::datum_t> data;
	for(int x : {0, 1, 2, 3, 5}) data.emplace_back(x, x*x + 1);
	const double C = std::getenv("C") ? std::atof(std::getenv("C")) : 13.0;
	const double LMAX = data.size() * std::log(ALPHA + (1-ALPHA)/NOUT); // exact max log likelihood

	for(double T : {1.0, 3.0, 20.0}) {
		auto want = brute(C, T, data);
		print("T", T, "brute-force programs with score >= -C:", want.size());

		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C;
		  AS a(data, P); AS::Status st;
		  auto got = run_astar(a, &st);
		  CHECK(st == AS::Status::Exhausted, "exhausted");
		  compare(got, want, -INFINITY, "default T=" + std::to_string(T));
		  print("  default: expansions", a.stats().expansions, "evaluated", a.stats().evaluated, "peak frontier", a.stats().peak_frontier); }

		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C; P.max_loglik = LMAX;
		  AS a(data, P);
		  compare(run_astar(a), want, -INFINITY, "tight max_loglik T=" + std::to_string(T));
		  print("  tight:   expansions", a.stats().expansions, "evaluated", a.stats().evaluated); }

		// admissible partial bound: a program whose root is lt/if/... can't beat LMAX; additionally, if the partial
		// program is a constant-only prefix it can't be checked -- so use a genuinely informative but valid bound:
		// a COMPLETE subtree at the root fixes the output, so evaluate it; otherwise LMAX.
		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C; P.max_loglik = 0.0;
		  AS a(data, P, [&](const Node& partial) -> double {
			  if(not partial.is_complete()) return LMAX;
			  MyHypothesis h{Node(partial)}; return h.compute_likelihood(data); });
		  compare(run_astar(a), want, -INFINITY, "partial bound T=" + std::to_string(T)); }

		// tiny frontier: results exact above exact_score_bound()
		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C; P.max_frontier = 40;
		  AS a(data, P);
		  auto got = run_astar(a);
		  CHECK(a.stats().dropped > 0, "tiny frontier should drop");
		  compare(got, want, a.exact_score_bound(), "tiny frontier T=" + std::to_string(T));
		  print("  tiny frontier: dropped", a.stats().dropped, "exact above", a.exact_score_bound(), "gcs", a.stats().gcs); }

		// stop and resume == one run
		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C;
		  AS a(data, P); AS::Status st;
		  auto g1 = run_astar(a, &st, 7);
		  CHECK(st == AS::Status::Stopped, "stopped");
		  auto g2 = run_astar(a);
		  g1.insert(g1.end(), g2.begin(), g2.end());
		  compare(g1, want, -INFINITY, "resume T=" + std::to_string(T)); }

		// expansion budget then continue
		{ AS::Params P; P.likelihood_temperature = T; P.min_score = -C; P.max_expansions = 50;
		  AS a(data, P); AS::Status st;
		  auto g1 = run_astar(a, &st);
		  CHECK(st == AS::Status::Budget or st == AS::Status::Exhausted, "budget");
		  for(size_t i = 0; i < g1.size() and i < want.size(); i++) CHECK(std::abs(g1[i].score - want[i].score) < 1e-9, "budget prefix", i); }
	}

	// inadmissible bounds must be detected
	{ AS::Params P; P.min_score = -C; P.max_loglik = -1000; // far below the true likelihoods
	  bool threw = false; try { AS a(data, P); run_astar(a); } catch(std::logic_error&) { threw = true; }
	  CHECK(threw, "inadmissible max_loglik not detected"); }
	{ AS::Params P; P.max_expansions = 100000; // no min_score: the search must reach completions under the bad bound
	  bool threw = false;
	  try { AS a(data, P, [](const Node& n) { return n.is_null() ? 0.0 : -500.0; }); run_astar(a); } catch(std::logic_error&) { threw = true; }
	  CHECK(threw, "inadmissible LikelihoodBound not detected"); }

	// min completion costs: hmin(int) is the cheapest int terminal (x), hmin(bool) = T
	{ AS a(data);
	  CHECK(std::abs(a.min_completion_cost(grammar.nt<int>()) - (-(grammar.rules[grammar.nt<int>()][0].logp - grammar.logZ[grammar.nt<int>()]))) < 1e-12, "hmin int"); }

	print(nfail == 0 ? "ALL TESTS PASSED" : "FAILURES", nchecks, "checks,", nfail, "failures");
	return nfail != 0;
}
