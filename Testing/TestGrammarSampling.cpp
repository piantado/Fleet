// TestGrammarSampling: rules are sampled with the right probabilities, whole trees are generated
// with the right probabilities, and Grammar::log_probability agrees with an independent computation.
//
// Every expected probability here comes from a weight table written out separately in this file
// (RuleSpec below), NOT from the Grammar's own Z/logZ/p fields, so the test does not trust the code
// under test to describe itself.
//
//  1. Grammar bookkeeping: Z, logZ, rule sort order, iteration, counts, get_rule semantics.
//  2. sample_rule frequencies per nonterminal (chi-square), zero-weight rules never sampled.
//  3. A FINITE grammar (305 trees): enumerate every tree, check log_probability for each against
//     the weight table, check the probabilities sum to 1, and check generate()'s empirical
//     distribution over all 305 trees (chi-square).
//  4. A RECURSIVE grammar: per-node lp, log_probability of random trees, and generate()'s
//     empirical distribution over its most common trees (with the exact remaining mass pooled).
//  5. change_probability: sampling and log_probability follow the new weights.
//  6. Partial trees: neighbor_prior sums to probability 1 over all expansions.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Fleet.h"
#include "Grammar.h"
#include "Builtins.h"

#include "TestHarness.h"

using namespace TestHarness;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The independent description of each grammar
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct RuleSpec { int nt; std::string format; std::vector<int> children; double w; };

struct Spec {
	std::vector<RuleSpec> rules;
	double Z(int nt) const { double z = 0; for(auto& r : rules) if(r.nt == nt) z += r.w; return z; }
	const RuleSpec& find(int nt, const std::string& fmt) const {
		for(auto& r : rules) if(r.nt == nt and r.format == fmt) return r;
		throw std::runtime_error("no spec for " + fmt);
	}
	double lp(int nt, const std::string& fmt) const { return std::log(find(nt,fmt).w) - std::log(Z(nt)); }
	// independent log probability of a tree: sum over nodes of log(w/Z), by explicit recursion
	double tree_lp(const Node& n) const {
		double s = lp(n.nt(), n.rule->format);
		for(size_t i=0;i<n.nchildren();i++) s += tree_lp(n.child(i));
		return s;
	}
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Finite grammar: nonterminals int(0), bool(1), short(2). No recursion, 305 trees with p>0.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class FiniteGrammar : public Grammar<int,int, int,bool,short> {
public:
	FiniteGrammar() {
		add("plus(%s,%s)",    +[](short a, short b) -> int { return a+b; }, 2.0);
		add("ite(%s,%s,%s)",  +[](bool c, short a, short b) -> int { return c ? a : b; }, 1.0);
		add("k",              +[]() -> int { return 7; }, 1.5);
		add("zero",           +[]() -> int { return 0; }, 0.0);    // zero weight: must never be generated
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("F",              +[]() -> bool { return false; }, 3.0);
		add("lt(%s,%s)",      +[](short a, short b) -> bool { return a<b; }, 0.5);
		add("1",              +[]() -> short { return 1; }, 1.0);
		add("2",              +[]() -> short { return 2; }, 2.0);
		add("3",              +[]() -> short { return 3; }, 3.0);
		add("4",              +[]() -> short { return 4; }, 1e-3); // rare
	}
} finite_grammar;

const Spec finite_spec = {{
	{0,"plus(%s,%s)",{2,2},2.0}, {0,"ite(%s,%s,%s)",{1,2,2},1.0}, {0,"k",{},1.5}, {0,"zero",{},0.0},
	{1,"T",{},1.0}, {1,"F",{},3.0}, {1,"lt(%s,%s)",{2,2},0.5},
	{2,"1",{},1.0}, {2,"2",{},2.0}, {2,"3",{},3.0}, {2,"4",{},1e-3}
}};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Recursive grammar: nonterminals int(0), bool(1)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class RecursiveGrammar : public Grammar<int,int, int,bool> {
public:
	RecursiveGrammar() {
		add("add(%s,%s)",     +[](int a, int b) -> int { return a+b; }, 1.0);
		add("neg(%s)",        +[](int a) -> int { return -a; }, 0.5);
		add("x",              Builtins::X<RecursiveGrammar>, 2.0);
		add("c",              +[]() -> int { return 3; }, 1.5);
		add("n",              +[]() -> int { return 5; }, 0.25);  // "n" is a prefix of "neg(%s)" and "not(%s)"
		add("if(%s,%s,%s)",   Builtins::If<RecursiveGrammar,int>, 0.3);
		add("and(%s,%s)",     Builtins::And<RecursiveGrammar>, 0.4);
		add("not(%s)",        Builtins::Not<RecursiveGrammar>, 0.4);
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a<b; }, 0.3);
		add("true",           +[]() -> bool { return true; }, 1.0);
		add("false",          +[]() -> bool { return false; }, 1.0);
	}
} rec_grammar;

Spec rec_spec = {{
	{0,"add(%s,%s)",{0,0},1.0}, {0,"neg(%s)",{0},0.5}, {0,"x",{},2.0}, {0,"c",{},1.5}, {0,"n",{},0.25}, {0,"if(%s,%s,%s)",{1,0,0},0.3},
	{1,"and(%s,%s)",{1,1},0.4}, {1,"not(%s)",{1},0.4}, {1,"lt(%s,%s)",{0,0},0.3}, {1,"true",{},1.0}, {1,"false",{},1.0}
}};

// enumerate every tree of nonterminal nt in the finite grammar, built from the SPEC (not by
// iterating the grammar's own rule list), with its probability computed from the spec
template<typename G>
std::vector<std::pair<Node,double>> enumerate(G& g, const Spec& spec, int nt) {
	std::vector<std::pair<Node,double>> out;
	for(const auto& rs : spec.rules) {
		if(rs.nt != nt) continue;
		const double lp = std::log(rs.w) - std::log(spec.Z(nt));
		std::vector<std::pair<Node,double>> partial = { {g.makeNode(g.get_rule((nonterminal_t)nt, rs.format)), lp} };
		for(size_t i=0;i<rs.children.size();i++) {
			auto kids = enumerate(g, spec, rs.children[i]);
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

// enumerate every NON-TERMINAL SKELETON (tree of non-terminal rules with holes where terminals go) of nonterminal nt,
// with its probability from the spec: a hole of type t has probability (sum of terminal weights of t)/Z(t),
// and a non-terminal rule has probability w/Z times the probabilities of its children's skeletons
template<typename G>
std::vector<std::pair<Node,double>> enumerate_nonterminal_skeletons(G& g, const Spec& spec, int nt) {
	std::vector<std::pair<Node,double>> out;
	double wterm = 0; for(const auto& rs : spec.rules) if(rs.nt == nt and rs.children.empty()) wterm += rs.w;
	if(wterm > 0) out.push_back({Node(), std::log(wterm) - std::log(spec.Z(nt))});
	for(const auto& rs : spec.rules) {
		if(rs.nt != nt or rs.children.empty() or rs.w == 0) continue;
		const double lp = std::log(rs.w) - std::log(spec.Z(nt));
		std::vector<std::pair<Node,double>> partial = { {g.makeNode(g.get_rule((nonterminal_t)nt, rs.format)), lp} };
		for(size_t i=0;i<rs.children.size();i++) {
			auto kids = enumerate_nonterminal_skeletons(g, spec, rs.children[i]);
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

// a copy of t with every terminal replaced by a null node (a terminal root becomes a null Node)
Node strip_terminals(const Node& t) {
	if(t.rule->is_terminal()) return Node();
	Node out = t;
	for(size_t i=0;i<t.nchildren();i++) out.set_child(i, strip_terminals(t.child(i)));
	return out;
}

// exact structural identity: same rule pointers, same stored lp, same shape
bool identical(const Node& a, const Node& b) {
	if(a.rule != b.rule or a.lp != b.lp or a.nchildren() != b.nchildren()) return false;
	for(size_t i=0;i<a.nchildren();i++) if(not identical(a.child(i), b.child(i))) return false;
	return true;
}

// t agrees with partial tree p everywhere p is non-null (null nodes of p match anything)
bool is_prefix(const Node& p, const Node& t) {
	if(p.is_null()) return true;
	if(p.rule != t.rule or p.lp != t.lp or p.nchildren() != t.nchildren()) return false;
	for(size_t i=0;i<p.nchildren();i++) if(not is_prefix(p.child(i), t.child(i))) return false;
	return true;
}

// every non-null node is a non-terminal rule; every null node sits in a slot whose type has a terminal of
// positive weight (or is the root, when nt itself has one); and the tree has the right type
template<typename G>
bool well_formed_nonterminal_skeleton(const G& g, const Node& t, nonterminal_t nt) {
	auto has_terminal = [&](nonterminal_t x) { for(auto& r : g.rules[x]) if(r.is_terminal() and r.p > 0) return true; return false; };
	if(t.is_null()) return t.is_root() and has_terminal(nt);
	if(t.nt() != nt) return false;
	for(auto& n : t) {
		if(n.is_null()) {
			if(n.is_root() or not has_terminal(n.parent->rule->type(n.pi))) return false;
		}
		else if(n.rule->is_terminal() or n.nchildren() != n.rule->N) return false;
	}
	return true;
}

template<typename G>
void check_bookkeeping(G& g, const Spec& spec, const std::string& gname) {
	for(size_t nt=0; nt<G::N_NTs; nt++) {
		CHECK_CLOSE(g.Z[nt], spec.Z(nt), 1e-15, 0, gname << " Z[" << nt << "]");
		CHECK_CLOSE(g.logZ[nt], std::log(spec.Z(nt)), 1e-15, 1e-15, gname << " logZ[" << nt << "]");
		CHECK_CLOSE(g.rule_normalizer(nt), spec.Z(nt), 1e-15, 0, gname << " rule_normalizer");
		size_t nspec = 0; for(auto& r : spec.rules) nspec += (r.nt == (int)nt);
		CHECK(g.count_rules(nt) == nspec, gname << " count_rules(" << nt << ")");
		// documented order: terminals first, then decreasing probability
		const auto& rv = g.rules[nt];
		for(size_t i=0;i+1<rv.size();i++) {
			const bool ti = rv[i].is_terminal(), tj = rv[i+1].is_terminal();
			CHECK(ti >= tj, gname << " terminals must come first: " << rv[i].format << " before " << rv[i+1].format);
			if(ti == tj) CHECK(rv[i].p >= rv[i+1].p, gname << " decreasing p: " << rv[i].format << " before " << rv[i+1].format);
		}
		for(const auto& r : rv) {
			const auto& s = spec.find(nt, r.format);
			CHECK(r.p == s.w and r.N == s.children.size() and r.nt == nt, gname << " rule fields " << r.format);
			CHECK(r.logp == std::log(s.w), gname << " cached logp " << r.format);
			for(size_t i=0;i<r.N;i++) CHECK((int)r.type(i) == s.children[i], gname << " child type " << r.format << " " << i);
			CHECK(g.get_rule((nonterminal_t)nt, r.format) == &r, gname << " get_rule(nt, format) " << r.format);
			CHECK(g.get_index_of(&r) == (size_t)(&r - &rv[0]), gname << " get_index_of " << r.format);
		}
	}
	size_t iterated = 0; int last_nt = -1; bool ordered = true;
	for(auto& r : g) { iterated++; ordered = ordered and (int)r.nt >= last_nt; last_nt = r.nt; }
	CHECK(iterated == g.count_rules() and iterated == spec.rules.size(), gname << " rule iteration visits " << iterated << " of " << spec.rules.size());
	CHECK(ordered, gname << " rule iteration is in nonterminal order");
}

// check the distribution of sample_rule(nt) against the spec weights
template<typename G>
void check_sample_rule(G& g, const Spec& spec, int nt, size_t N, const std::string& gname) {
	std::map<std::string,double> cnt;
	for(size_t i=0;i<N;i++) cnt[g.sample_rule((nonterminal_t)nt)->format]++;
	std::vector<double> obs, pr;
	for(auto& r : spec.rules) {
		if(r.nt != nt) continue;
		obs.push_back(cnt[r.format]);
		pr.push_back(r.w);
		if(r.w == 0) CHECK(cnt[r.format] == 0, gname << " zero-weight rule " << r.format << " was sampled " << cnt[r.format] << " times");
		else CHECK_Z(binomial_z(cnt[r.format], N, r.w/spec.Z(nt)), gname << " sample_rule frequency of " << r.format);
	}
	CHECK_PVALUE(chisq_test(obs, pr, gname + " sample_rule(" + str(nt) + ")"), gname << " sample_rule(" << nt << ") distribution");
}

int main() {
	test_name = "GrammarSampling";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("bookkeeping: Z, logZ, sort order, iteration, get_rule");
	check_bookkeeping(finite_grammar, finite_spec, "finite");
	check_bookkeeping(rec_grammar, rec_spec, "recursive");
	CHECK(finite_grammar.start() == 0 and finite_grammar.nt<bool>() == 1 and finite_grammar.nt<short>() == 2, "nt<T>() indices");
	CHECK(finite_grammar.count_terminals(0) == 2 and finite_grammar.count_nonterminals((nonterminal_t)0) == 2, "count_terminals/count_nonterminals");
	CHECK(rec_grammar.get_rule("ad") == rec_grammar.get_rule((nonterminal_t)0, "add(%s,%s)"), "get_rule by unique prefix");
	CHECK(throws([]{ (void)rec_grammar.get_rule("zzz"); }), "get_rule of a missing rule throws");
	CHECK(rec_grammar.get_rule((nonterminal_t)1, "n")->format == "not(%s)", "get_rule(nt, s) only considers rules of type nt");
	CHECK(throws([]{ (void)rec_grammar.get_rule((nonterminal_t)1, ""); }), "get_rule(nt, \"\") must match exactly (and here matches nothing)");
	CHECK(rec_grammar.get_rule((nonterminal_t)0, "n")->format == "n", "get_rule(nt, s) prefers the exact match \"n\" over the prefix match \"neg(%s)\"");
	// the untyped get_rule(s) should agree with get_rule(nt,s) about exact matches
	CHECK(not dies([]{ volatile auto r = rec_grammar.get_rule("n"); (void)r; }),
	      "get_rule(\"n\") asserts because \"n\" is also a prefix of \"neg(%s)\"/\"not(%s)\", although a rule matches \"n\" EXACTLY; "
	      "get_rule(nt,s) handles this, but get_rule(s) -- used by change_probability and simple_parse -- does not");

	// sample_rule is documented to be sample<Rule>(rules, Z, p) specialized, consuming identical randomness
	{
		bool same = true;
		std::function<double(const Rule&)> f = [](const Rule& r){ return r.p; };
		for(int i=0;i<20000;i++) {
			const nonterminal_t nt = (nonterminal_t)(i % 3);
			DefaultRNG.seed(5000+i); const Rule* a = finite_grammar.sample_rule(nt);
			DefaultRNG.seed(5000+i); const Rule* b = sample<Rule>(finite_grammar.rules[nt], finite_grammar.Z[nt], f).first;
			same = same and a == b;
		}
		CHECK(same, "sample_rule selects exactly what sample<Rule>(rules, Z, p) selects");
		DefaultRNG.seed(seed()+1);
	}

	// ==================================================================================
	section("sample_rule frequencies");
	for(int nt=0; nt<3; nt++) check_sample_rule(finite_grammar, finite_spec, nt, 3000000, "finite");
	for(int nt=0; nt<2; nt++) check_sample_rule(rec_grammar, rec_spec, nt, 3000000, "recursive");

	// ==================================================================================
	section("finite grammar: every tree's log probability, and generate()'s distribution");
	{
		auto all = enumerate(finite_grammar, finite_spec, 0);
		std::map<std::string,double> exact;   // string -> probability
		long double total = 0;
		for(auto& [t, lp] : all) {
			if(lp == -infinity) continue; // the zero-weight rule
			CHECK_CLOSE(finite_grammar.log_probability(t), lp, 1e-13, 1e-13, "log_probability of " << t.string());
			CHECK(t.is_complete() and t.count_nonnull() == t.count(), "enumerated tree complete");
			CHECK(exact.count(t.string()) == 0, "tree strings are unique: " << t.string());
			exact[t.string()] = std::exp(lp);
			total += expl(lp);
		}
		CHECK(exact.size() == 305, "finite grammar has 305 trees of nonzero probability, enumerated " << exact.size());
		CHECK_CLOSE((double)total, 1.0, 1e-12, 0, "tree probabilities sum to 1");

		const size_t N = 4000000;
		std::map<std::string,double> obs;
		bool lp_ok = true, unknown = false;
		for(size_t i=0;i<N;i++) {
			Node t = finite_grammar.generate();
			const std::string s = t.string();
			if(not exact.count(s)) { unknown = true; continue; }
			obs[s]++;
			if(i < 20000) {
				// each node's stored lp is log(p/Z) of its rule
				for(auto& n : t) lp_ok = lp_ok and close(n.lp, finite_spec.lp(n.nt(), n.rule->format), 1e-14, 1e-14);
			}
		}
		CHECK(not unknown, "generate() produced a tree outside the grammar's support (e.g. the zero-weight rule)");
		CHECK(lp_ok, "Node::lp set by makeNode equals log(p/Z) of its rule");
		std::vector<double> o, p;
		for(auto& [s, pr] : exact) { o.push_back(obs[s]); p.push_back(pr); }
		CHECK_PVALUE(chisq_test(o, p, "generate() over all 305 finite trees"), "generate() tree distribution (finite grammar)");
		// generate from a non-start nonterminal
		std::map<std::string,double> bobs;
		for(size_t i=0;i<1000000;i++) bobs[finite_grammar.generate((nonterminal_t)1).string()]++;
		auto ball = enumerate(finite_grammar, finite_spec, 1);
		std::vector<double> bo, bp;
		for(auto& [t, lp] : ball) { bo.push_back(bobs[t.string()]); bp.push_back(std::exp(lp)); }
		CHECK_PVALUE(chisq_test(bo, bp, "generate(bool) over 18 trees"), "generate(nt) distribution");
	}

	// ==================================================================================
	section("recursive grammar: per-node lp, log_probability, generate()'s distribution");
	{
		const size_t N = 3000000;
		std::map<std::string,double> obs;
		std::map<std::string,double> lp_of;
		bool ok_lp = true, ok_node = true;
		for(size_t i=0;i<N;i++) {
			Node t = rec_grammar.generate();
			const std::string s = t.string();
			obs[s]++;
			if(not lp_of.count(s)) {
				const double ref = rec_spec.tree_lp(t);
				lp_of[s] = ref;
				ok_lp = ok_lp and close(rec_grammar.log_probability(t), ref, 1e-12, 1e-12);
				for(auto& n : t) ok_node = ok_node and close(n.lp, rec_spec.lp(n.nt(), n.rule->format), 1e-14, 1e-14);
				// log_probability is also the sum of the stored per-node lps
				double s2 = 0; for(auto& n : t) s2 += n.lp;
				ok_lp = ok_lp and close(s2, ref, 1e-12, 1e-12);
			}
		}
		CHECK(ok_lp, "log_probability of every distinct generated tree matches the independent computation");
		CHECK(ok_node, "per-node lp of every distinct generated tree");
		// chi-square over the trees whose expected count is >= 20, with ALL remaining mass (exact) pooled
		std::vector<std::pair<double,std::string>> byp;
		for(auto& [s, lp] : lp_of) byp.push_back({lp, s});
		std::sort(byp.rbegin(), byp.rend());
		std::vector<double> o, p; double used = 0, used_obs = 0;
		for(auto& [lp, s] : byp) {
			const double pr = std::exp(lp);
			if(pr*N < 20) break;
			o.push_back(obs[s]); p.push_back(pr); used += pr; used_obs += obs[s];
		}
		o.push_back(N - used_obs); p.push_back(1.0 - used);
		note("testing " + str(o.size()-1) + " individual trees covering mass " + str(used));
		CHECK(o.size() > 50, "enough individual trees to test");
		CHECK_PVALUE(chisq_test(o, p, "generate() over common recursive trees + exact remainder"), "generate() tree distribution (recursive grammar)");

		// prior probability of the root rule and of the root's first child's rule (a depth-2 check)
		std::map<std::string,double> root, pair2;
		for(size_t i=0;i<1000000;i++) {
			Node t = rec_grammar.generate();
			root[t.rule->format]++;
			if(t.nchildren() > 0) pair2[t.rule->format + "|" + t.child(0).rule->format]++;
		}
		std::vector<double> ro, rp;
		for(auto& r : rec_spec.rules) if(r.nt == 0) { ro.push_back(root[r.format]); rp.push_back(r.w); }
		CHECK_PVALUE(chisq_test(ro, rp, "root rule"), "root rule distribution");
		std::vector<double> po, pp;
		for(auto& r : rec_spec.rules) if(r.nt == 0 and not r.children.empty()) {
			for(auto& c : rec_spec.rules) if(c.nt == r.children[0]) {
				po.push_back(pair2[r.format + "|" + c.format]);
				pp.push_back(r.w/rec_spec.Z(0) * c.w/rec_spec.Z(c.nt));
			}
		}
		CHECK_PVALUE(chisq_test(po, pp, "(root rule, first child rule) pairs"), "depth-2 rule pair distribution");

		// get_counts agrees with a direct count over nodes
		for(int i=0;i<200;i++) {
			Node t = rec_grammar.generate();
			auto counts = rec_grammar.get_counts(t);
			auto idx = rec_grammar.get_rule_indexer();
			std::vector<size_t> mine(rec_grammar.count_rules(), 0);
			for(auto& n : t) mine[idx.at(n.rule)]++;
			size_t tot = 0; for(auto c : counts) tot += c;
			CHECK(counts == mine and tot == t.count(), "get_counts");
		}
	}

	// ==================================================================================
	section("change_probability");
	{
		// change several weights, including to and from zero, and check everything follows
		rec_grammar.change_probability("c", 6.0);         rec_spec.rules[3].w = 6.0;
		rec_grammar.change_probability("neg(%s)", 0.0);   rec_spec.rules[1].w = 0.0;
		rec_grammar.change_probability("true", 0.05);     rec_spec.rules[9].w = 0.05;
		rec_grammar.change_probability("true", 2.5);      rec_spec.rules[9].w = 2.5;
		for(nonterminal_t nt=0; nt<2; nt++) {
			CHECK_CLOSE(rec_grammar.Z[nt], rec_spec.Z(nt), 1e-13, 0, "Z after change_probability (accumulated += / -= drift must stay tiny)");
			CHECK(rec_grammar.logZ[nt] == std::log(rec_grammar.Z[nt]), "logZ in sync with Z after change_probability");
			for(auto& r : rec_grammar.rules[nt]) CHECK(r.logp == std::log(r.p), "logp in sync with p for " << r.format);
		}
		for(int nt=0; nt<2; nt++) check_sample_rule(rec_grammar, rec_spec, nt, 2000000, "changed");
		bool ok = true, neg = false;
		for(int i=0;i<20000;i++) {
			Node t = rec_grammar.generate();
			for(auto& n : t) neg = neg or n.rule->format == "neg(%s)";
			ok = ok and close(rec_grammar.log_probability(t), rec_spec.tree_lp(t), 1e-12, 1e-12);
		}
		CHECK(ok, "log_probability follows the new weights");
		CHECK(not neg, "a rule changed to weight 0 is never generated");
	}

	// ==================================================================================
	section("generate_nonterminal_skeleton: non-terminal trees with holes where the terminals go");
	{
		// (a) with the same rng state, generate_nonterminal_skeleton(nt) is exactly generate(nt) with its terminals removed
		bool same = true, wf = true;
		for(int i=0;i<30000;i++) {
			const nonterminal_t nt = (nonterminal_t)(i % 3);
			DefaultRNG.seed(9000+i); Node full = finite_grammar.generate(nt);
			DefaultRNG.seed(9000+i); Node sk   = finite_grammar.generate_nonterminal_skeleton(nt);
			same = same and identical(sk, strip_terminals(full));
			wf   = wf and well_formed_nonterminal_skeleton(finite_grammar, sk, nt);
		}
		for(int i=0;i<30000;i++) {
			const nonterminal_t nt = (nonterminal_t)(i % 2);
			DefaultRNG.seed(70000+i); Node full = rec_grammar.generate(nt);
			DefaultRNG.seed(70000+i); Node sk   = rec_grammar.generate_nonterminal_skeleton(nt);
			same = same and identical(sk, strip_terminals(full));
			wf   = wf and well_formed_nonterminal_skeleton(rec_grammar, sk, nt);
		}
		CHECK(same, "generate_nonterminal_skeleton == generate with terminals removed, given the same rng state");
		CHECK(wf, "generate_nonterminal_skeleton returns only non-terminal rules, with holes exactly in slots that take terminals");

		// (b) the same holds through the DepthException/retry path: with a tiny max depth, many attempts are
		//     rejected, and both functions must reject and retry identically
		const size_t old_depth = rec_grammar.GRAMMAR_MAX_DEPTH;
		rec_grammar.GRAMMAR_MAX_DEPTH = 3;
		bool same_d = true, shallow = true;
		for(int i=0;i<20000;i++) {
			DefaultRNG.seed(200000+i); Node full = rec_grammar.generate();
			DefaultRNG.seed(200000+i); Node sk   = rec_grammar.generate_nonterminal_skeleton();
			same_d = same_d and identical(sk, strip_terminals(full));
			shallow = shallow and full.depth() <= 2; // depth() of a leaf is 0; recursion depth 3 throws
		}
		rec_grammar.GRAMMAR_MAX_DEPTH = old_depth;
		CHECK(shallow, "GRAMMAR_MAX_DEPTH=3 really restricts generate (sanity check of the retry test)");
		CHECK(same_d, "generate_nonterminal_skeleton == stripped generate through depth-exception retries");
		DefaultRNG.seed(seed()+2);

		// (c) its distribution, against exact skeleton probabilities computed from the spec (independent of (a))
		for(int nt=0; nt<2; nt++) {
			auto all = enumerate_nonterminal_skeletons(finite_grammar, finite_spec, nt);
			std::map<std::string,double> exact;
			long double total = 0;
			for(auto& [t, lp] : all) {
				CHECK(exact.count(t.string()) == 0, "skeleton strings are unique: " << t.string());
				exact[t.string()] = std::exp(lp);
				total += expl(lp);
				// log_probability skips holes: it is lp minus each hole's log(terminal mass / Z)
				double hole_lp = 0;
				if(t.is_null()) hole_lp = lp; // the whole tree is one hole
				else for(auto& n : t) if(n.is_null()) {
					const int ht = n.parent->rule->type(n.pi);
					double wt = 0; for(auto& r : finite_spec.rules) if(r.nt == ht and r.children.empty()) wt += r.w;
					hole_lp += std::log(wt) - std::log(finite_spec.Z(ht));
				}
				CHECK_CLOSE(finite_grammar.log_probability(t), lp - hole_lp, 1e-13, 1e-13, "log_probability of skeleton " << t.string());
			}
			CHECK_CLOSE((double)total, 1.0, 1e-12, 0, "skeleton probabilities sum to 1 for nt=" << nt);

			const size_t N = 2000000;
			std::map<std::string,double> obs;
			bool unknown = false, lp_ok = true;
			for(size_t i=0;i<N;i++) {
				Node t = finite_grammar.generate_nonterminal_skeleton((nonterminal_t)nt);
				const std::string s = t.string();
				if(not exact.count(s)) { unknown = true; continue; }
				obs[s]++;
				if(i < 20000) for(auto& n : t) if(not n.is_null()) lp_ok = lp_ok and close(n.lp, finite_spec.lp(n.nt(), n.rule->format), 1e-14, 1e-14);
			}
			CHECK(not unknown, "generate_nonterminal_skeleton produced a skeleton outside the support (nt=" << nt << ")");
			CHECK(lp_ok, "skeleton nodes carry lp = log(p/Z) of their rule");
			std::vector<double> o, p;
			for(auto& [s, pr] : exact) { o.push_back(obs[s]); p.push_back(pr); }
			CHECK_PVALUE(chisq_test(o, p, "generate_nonterminal_skeleton(" + str(nt) + ") over all " + str(exact.size()) + " skeletons"),
			             "generate_nonterminal_skeleton distribution (finite grammar, nt=" << nt << ")");
		}

		// (d) holes are ordinary gaps: complete() fills every hole (with a generated subtree of the hole's type)
		//     and leaves the skeleton's own nodes in place
		bool fill_ok = true;
		for(int i=0;i<5000;i++) {
			Node sk = rec_grammar.generate_nonterminal_skeleton();
			if(sk.is_null()) continue;
			Node t = sk;
			rec_grammar.complete(t);
			fill_ok = fill_ok and t.is_complete() and is_prefix(sk, t);
		}
		CHECK(fill_ok, "complete() fills every hole of a skeleton and keeps its non-terminal nodes");
	}

	// ==================================================================================
	section("generate / generate_nonterminal_skeleton when every attempt hits the depth limit");
	{
		// with GRAMMAR_MAX_DEPTH=0 every attempt throws a DepthException, so all retries fail. That must assert
		// (debug builds) or throw a DepthException (NDEBUG builds) -- never return garbage by falling off the end.
		const size_t old_depth = rec_grammar.GRAMMAR_MAX_DEPTH;
		rec_grammar.GRAMMAR_MAX_DEPTH = 0;
		#ifdef NDEBUG
			auto throws_depth = [](auto f) { try { f(); } catch(const DepthException&) { return true; } catch(...) { return false; } return false; };
			CHECK(throws_depth([]{ (void)rec_grammar.generate(); }), "generate throws DepthException after all retries fail (NDEBUG)");
			CHECK(throws_depth([]{ (void)rec_grammar.generate_nonterminal_skeleton(); }), "generate_nonterminal_skeleton throws DepthException after all retries fail (NDEBUG)");
		#else
			CHECK(dies([]{ (void)rec_grammar.generate(); }), "generate asserts after all retries fail");
			CHECK(dies([]{ (void)rec_grammar.generate_nonterminal_skeleton(); }), "generate_nonterminal_skeleton asserts after all retries fail");
		#endif
		rec_grammar.GRAMMAR_MAX_DEPTH = old_depth;
	}

	// ==================================================================================
	section("partial trees: neighbors, neighbor_prior, expand_to_neighbor, complete");
	{
		for(int rep=0; rep<200; rep++) {
			// make a partial tree: a generated tree with one random subtree replaced by a null node
			Node t = rec_grammar.generate();
			if(t.count() < 2) continue;
			Node* s = t.get_nth(1 + myrandom((int)t.count()-1));
			if(s->is_root()) continue;
			Node* par = s->parent; const size_t pi = s->pi;
			const nonterminal_t gap_nt = par->rule->type(pi);
			par->set_child(pi, Node()); // a null node
			CHECK(not t.is_complete(), "tree with a gap is not complete");
			const size_t nn = rec_grammar.neighbors(t);
			CHECK(nn == rec_grammar.count_rules(gap_nt), "neighbors = number of rules for the gap's type");
			long double mass = 0;
			for(int k=0;k<(int)nn;k++) {
				int which = k; const double lp = rec_grammar.neighbor_prior(t, which);
				mass += expl(lp);
				Node u = t; int w2 = k; rec_grammar.expand_to_neighbor(u, w2);
				// the expansion filled the gap with rule k of the gap's type, and added exactly lp
				CHECK_CLOSE(rec_grammar.log_probability(u) - rec_grammar.log_probability(t), lp, 1e-12, 1e-12, "expand_to_neighbor adds neighbor_prior");
			}
			CHECK_CLOSE((double)mass, 1.0, 1e-12, 0, "neighbor priors sum to probability 1");
			rec_grammar.complete(t);
			CHECK(t.is_complete(), "complete() fills every gap");
		}
	}

	return finish();
}
