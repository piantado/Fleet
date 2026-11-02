// TestRebag: the rebag proposal (src/Inference/Experimental/RebagProposal.h).
//
// Every reference quantity below is computed INDEPENDENTLY of RebagProposal.h: priors from the Spec
// weight tables, likelihoods from the tree-walking evaluators, and bag/count/Q values by brute-force
// enumeration of trees and of regeneration outcomes.
//
//  1. Counting / enumeration / uniform sampling of A(N, D) against brute force (finite and recursive grammars).
//  2. log_Q(B'|x) against the exact distribution of bags produced by regenerating one node of x.
//  3. The proposal distribution from fixed states against q(x->y) = Q(bag y|x) pi(y)/Z(bag y),
//     and every returned fb against log q(x->y) - log q(y->x) computed independently.
//  4. Stationarity of MCMCChain (through propose_tempered) against the exact posterior, in every mode:
//     exact, T=3, mixed with regeneration, sampled tries (K=1,2,3), fallback; and on a recursive
//     grammar with a node cap.
//  5. Mutants (Z dropped, Q dropped, bag prior dropped from Q) must FAIL the stationarity test.

#include "TestModels.h"
#include "Proposers.h"
#include "RebagProposal.h"

using namespace TestHarness;

// REBAG_QUICK=1 (environment): scale all sample sizes down ~50x. Only for running every code path
// under ASan/UBSan quickly -- the statistical verdicts are NOT meaningful in that mode.
const double QUICK = std::getenv("REBAG_QUICK") ? 0.02 : 1.0;
size_t scaled(double n) { return std::max<size_t>(50, (size_t)(n*QUICK)); }

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Hypotheses using rebag via propose_tempered
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct RebagConfig { Rebag::Options opt = verified(); double p_rebag = 1.0;
	// tests pin the mode explicitly (EXACT unless a section asks otherwise), independent of the library defaults
	static Rebag::Options verified() { Rebag::Options o; o.verify = true; o.enum_max = 1e18; o.tries = 0; return o; } };
inline RebagConfig finite_cfg; // read-only while chains run

class RebagFinite : public DeterministicLOTHypothesis<RebagFinite, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<RebagFinite, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		const int out = call(d.input, -999);
		return std::log(ALPHA*(out == d.output) + (1-ALPHA)/NOUT);
	}
	[[nodiscard]] ProposalType propose_tempered(const data_t& data, double T) const {
		if(flip(finite_cfg.p_rebag)) return Proposals::rebag(*this, data, T, finite_cfg.opt);
		return Super::propose();
	}
};

// A recursive grammar (int, bool), as in TestMCMC
class RGrammar : public Grammar<int,int, int,bool> {
public:
	RGrammar() {
		add("add(%s,%s)",     +[](int a, int b) -> int { return a+b; }, 1.0);
		add("sub(%s,%s)",     +[](int a, int b) -> int { return a-b; }, 0.5);
		add("neg(%s)",        +[](int a) -> int { return -a; }, 0.4);
		add("x",              Builtins::X<RGrammar>, 2.0);
		add("one",            +[]() -> int { return 1; }, 1.5);
		add("if(%s,%s,%s)",   Builtins::If<RGrammar,int>, 0.3);
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a<b; }, 0.5);
		add("and(%s,%s)",     Builtins::And<RGrammar>, 0.3);
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("F",              +[]() -> bool { return false; }, 1.0);
	}
} r_grammar;

const Spec r_spec = {{
	{0,"add(%s,%s)",{0,0},1.0}, {0,"sub(%s,%s)",{0,0},0.5}, {0,"neg(%s)",{0},0.4}, {0,"x",{},2.0}, {0,"one",{},1.5}, {0,"if(%s,%s,%s)",{1,0,0},0.3},
	{1,"lt(%s,%s)",{0,0},0.5}, {1,"and(%s,%s)",{1,1},0.3}, {1,"T",{},1.0}, {1,"F",{},1.0}
}};

long r_eval(const Node& n, long x) {
	const std::string& f = n.rule->format;
	if(f == "add(%s,%s)")    return r_eval(n.child(0),x) + r_eval(n.child(1),x);
	if(f == "sub(%s,%s)")    return r_eval(n.child(0),x) - r_eval(n.child(1),x);
	if(f == "neg(%s)")       return -r_eval(n.child(0),x);
	if(f == "x")             return x;
	if(f == "one")           return 1;
	if(f == "if(%s,%s,%s)")  return r_eval(n.child(0),x) ? r_eval(n.child(1),x) : r_eval(n.child(2),x);
	if(f == "lt(%s,%s)")     return r_eval(n.child(0),x) < r_eval(n.child(1),x);
	if(f == "and(%s,%s)")    return r_eval(n.child(0),x) and r_eval(n.child(1),x);
	if(f == "T")             return 1;
	if(f == "F")             return 0;
	throw std::runtime_error("r_eval: " + f);
}
constexpr double R_ALPHA = 0.3;
inline RebagConfig r_cfg;

class RebagRec : public DeterministicLOTHypothesis<RebagRec, int, int, RGrammar, &r_grammar> {
public:
	using Super = DeterministicLOTHypothesis<RebagRec, int, int, RGrammar, &r_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		return std::log(R_ALPHA*(call(d.input, -999) == d.output) + (1-R_ALPHA)/NOUT);
	}
	[[nodiscard]] ProposalType propose_tempered(const data_t& data, double T) const {
		if(flip(r_cfg.p_rebag)) return Proposals::rebag(*this, data, T, r_cfg.opt);
		return Super::propose();
	}
};
std::vector<RebagRec::datum_t> r_data = { RebagRec::datum_t(0, 1), RebagRec::datum_t(1, 2), RebagRec::datum_t(3, 4) };

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Independent helpers
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// a bag as a sorted list of format strings (independent of Rebag::Bag)
std::string bagkey(const Node& n) {
	std::vector<std::string> v;
	for(auto& x : n) v.push_back(std::to_string(x.nt()) + ":" + x.rule->format);
	std::sort(v.begin(), v.end());
	std::string s; for(auto& x : v) { s += x; s += ";"; }
	return s;
}

// all trees of exact size n rooted at nt, from the Spec (independent of the Tables DP)
template<typename G>
std::vector<Node> trees_of_size(G& g, const Spec& spec, int nt, int n,
                                std::map<std::pair<int,int>, std::vector<Node>>& memo) {
	if(auto it = memo.find({nt,n}); it != memo.end()) return it->second;
	std::vector<Node> out;
	for(const auto& rs : spec.rules) {
		if(rs.nt != nt) continue;
		const int k = rs.children.size();
		if(k == 0) { if(n == 1) out.push_back(g.makeNode(g.get_rule((nonterminal_t)nt, rs.format))); continue; }
		// compositions of n-1 into k positive parts
		std::vector<int> part(k, 1);
		std::function<void(int,int)> rec = [&](int i, int left) {
			if(i == k-1) {
				if(left < 1) return;
				part[i] = left;
				std::vector<std::vector<Node>> kids;
				for(int j=0;j<k;j++) kids.push_back(trees_of_size(g, spec, rs.children[j], part[j], memo));
				std::vector<Node> partial = { g.makeNode(g.get_rule((nonterminal_t)nt, rs.format)) };
				for(int j=0;j<k;j++) {
					std::vector<Node> next;
					for(auto& p : partial) for(auto& c : kids[j]) { Node q = p; q.set_child(j, c); next.push_back(q); }
					partial = std::move(next);
				}
				for(auto& q : partial) out.push_back(q);
				return;
			}
			for(int a=1; a<=left-(k-1-i); a++) { part[i] = a; rec(i+1, left-a); }
		};
		rec(0, n-1);
	}
	memo[{nt,n}] = out;
	return out;
}

using Path = std::vector<size_t>;
void all_paths(const Node& n, Path& cur, std::vector<Path>& out) {
	out.push_back(cur);
	for(size_t i=0;i<n.nchildren();i++) { cur.push_back(i); all_paths(n.child(i), cur, out); cur.pop_back(); }
}
std::vector<Path> all_paths(const Node& n) { std::vector<Path> out; Path p; all_paths(n, p, out); return out; }
const Node& at(const Node& n, const Path& p) { const Node* c = &n; for(auto i : p) c = &c->child(i); return *c; }
Node replaced(const Node& n, const Path& p, const Node& s) {
	Node out = n; Node* c = &out; for(auto i : p) c = &c->child(i); c->assign(Node(s)); return out;
}

// Exact regeneration bag distribution from x, for bags with total <= L:
//   Qx[bagkey] = (1/|x|) sum_m sum_{s: N_m, |s| <= L} P_spec(s) [bag(x[m<-s]) == bagkey]
template<typename G>
std::map<std::string,double> exact_Q(G& g, const Spec& spec, const Node& x, int L,
                                     std::map<std::pair<int,int>, std::vector<Node>>& memo) {
	std::map<std::string,double> Q;
	auto paths = all_paths(x);
	for(auto& p : paths) {
		const Node& m = at(x, p);
		const int ctx = (int)x.count() - (int)m.count();
		for(int sz=1; sz + ctx <= L; sz++)
			for(auto& s : trees_of_size(g, spec, m.nt(), sz, memo))
				Q[bagkey(replaced(x, p, s))] += std::exp(spec.tree_lp(s)) / paths.size();
	}
	return Q;
}

double lse(const std::vector<double>& v) {
	double m = -INFINITY; for(double x : v) m = std::max(m, x);
	if(m == -INFINITY) return m;
	double s = 0; for(double x : v) s += std::exp(x-m);
	return m + std::log(s);
}

// quiet version of compare_to_exact for mutants (which are SUPPOSED to fail): returns #bad states
int count_bad_states(const std::vector<Counts>& reps, const std::map<std::string,double>& exact, double minp=2e-3, double tmax=7.0) {
	const int K = reps.size(); std::vector<double> tot(K, 0.0);
	for(int k=0;k<K;k++) for(auto& [s,c] : reps[k]) tot[k] += c;
	int bad = 0;
	for(auto& [s,p] : exact) {
		if(p < minp) continue;
		double m=0, m2=0;
		for(int k=0;k<K;k++) { double f = reps[k].count(s) ? reps[k].at(s)/tot[k] : 0.0; m += f; m2 += f*f; }
		m /= K; const double se = std::sqrt(std::max(0.0,(m2-K*m*m)/(K-1)))/std::sqrt((double)K);
		if(std::abs(se > 0 ? (m-p)/se : (m==p ? 0 : INFINITY)) > tmax) bad++;
	}
	return bad;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main() {
	test_name = "Rebag";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	auto& ftab = Rebag::tables_for(&finite_grammar);
	auto& rtab = Rebag::tables_for(&r_grammar);

	// ==================================================================================
	section("counting and enumeration vs brute force: finite grammar (all 3 nonterminals)");
	{
		int bad = 0, nbags = 0;
		for(int nt=0; nt<3; nt++) {
			std::map<std::string, std::vector<Node>> groups;
			for(auto& [t, lp] : enumerate_trees(finite_grammar, finite_spec, nt)) groups[bagkey(t)].push_back(t);
			for(auto& [k, ts] : groups) {
				nbags++;
				const Rebag::Bag B = ftab.bag_of(ts[0]);
				const double c = ftab.count(B, (nonterminal_t)nt);
				std::set<std::string> want, got;
				for(auto& t : ts) want.insert(t.parseable());
				for(auto& t : ftab.enumerate(B, (nonterminal_t)nt)) got.insert(t.parseable());
				if(c != ts.size() or want != got) { if(bad++ < 3) CHECK(false, "bag " << k << " count " << c << " vs " << ts.size()); }
				// the same bag rooted at a different nonterminal has no trees
				for(int nt2=0; nt2<3; nt2++) if(nt2 != nt) CHECK(ftab.count(B, (nonterminal_t)nt2) == 0, "bag " << k << " rooted at wrong nt");
			}
		}
		CHECK(bad == 0, bad << " finite bags miscounted or misenumerated");
		note("checked " + str(nbags) + " bags");
	}

	// ==================================================================================
	section("counting and enumeration vs brute force: recursive grammar, all trees of size <= 7");
	std::map<std::pair<int,int>, std::vector<Node>> rmemo;
	{
		int bad = 0, nbags = 0; size_t ntrees = 0; double maxc = 0;
		for(int nt=0; nt<2; nt++) {
			std::map<std::string, std::vector<Node>> groups;
			for(int n=1;n<=7;n++) for(auto& t : trees_of_size(r_grammar, r_spec, nt, n, rmemo)) { groups[bagkey(t)].push_back(t); ntrees++; }
			for(auto& [k, ts] : groups) {
				nbags++;
				const Rebag::Bag B = rtab.bag_of(ts[0]);
				const double c = rtab.count(B, (nonterminal_t)nt);
				maxc = std::max(maxc, c);
				bool ok = (c == ts.size());
				if(ts.size() <= 2000) { // enumeration check on all but the very largest bags
					std::set<std::string> want, got;
					for(auto& t : ts) want.insert(t.parseable());
					for(auto& t : rtab.enumerate(B, (nonterminal_t)nt)) got.insert(t.parseable());
					ok = ok and want == got;
				}
				if(not ok and bad++ < 3) CHECK(false, "bag " << k << " count " << c << " vs " << ts.size());
			}
		}
		CHECK(bad == 0, bad << " recursive bags miscounted or misenumerated");
		note("checked " + str(nbags) + " bags over " + str(ntrees) + " trees; largest bag has " + str(maxc) + " trees");

		// bags with no tree: unbalanced, or balanced-but-impossible
		Rebag::Bag e;  CHECK(rtab.count(e, 0) == 0, "empty bag");
		Rebag::Bag b1 = rtab.bag_of(r_grammar.from_parseable("0:add(%s,%s);0:x;0:x")); b1.add(rtab.index.at(r_grammar.get_rule(0,"one")));
		CHECK(rtab.count(b1, 0) == 0, "unbalanced bag (one leaf too many)");
	}

	// ==================================================================================
	section("uniform sampling within a bag");
	{
		// the bag of add(add(x,one),sub(x,one)) etc: pick the largest size-7 int bag with <= 400 trees
		std::map<std::string, std::vector<Node>> groups;
		for(auto& t : trees_of_size(r_grammar, r_spec, 0, 7, rmemo)) groups[bagkey(t)].push_back(t);
		std::vector<Node>* best = nullptr;
		for(auto& [k, ts] : groups) if(ts.size() <= 400 and (best == nullptr or ts.size() > best->size())) best = &ts;
		const Rebag::Bag B = rtab.bag_of((*best)[0]);
		const size_t M = best->size(), N = scaled(400.0 * M);
		std::map<std::string,double> f;
		for(size_t i=0;i<N;i++) f[rtab.sample_uniform(B, 0).parseable()]++;
		double worst = 0; bool outside = false;
		std::set<std::string> support; for(auto& t : *best) support.insert(t.parseable());
		for(auto& [s,c] : f) if(not support.count(s)) outside = true;
		for(auto& s : support) {
			const double p = 1.0/M, z = (f[s]/N - p)/std::sqrt(p*(1-p)/N);
			worst = std::max(worst, std::abs(z));
		}
		note("bag with " + str(M) + " trees, " + str(N) + " draws, worst |z| = " + str(worst));
		CHECK(not outside, "uniform sampler produced a tree outside the bag");
		CHECK(f.size() == M, "uniform sampler missed some trees");
		CHECK(worst < 6.0, "uniform sampler deviates from uniform: worst |z| " << worst);
	}

	// ==================================================================================
	section("log_Q(B'|x) vs the exact regeneration bag distribution");
	{
		int bad = 0, n = 0; double worst = 0, worst_mass = 0;
		// finite grammar: every one of the 309 programs, every bag reachable (complete distribution)
		std::map<std::pair<int,int>, std::vector<Node>> fmemo;
		for(auto& [t, lp] : enumerate_trees(finite_grammar, finite_spec, 0)) {
			auto Q = exact_Q(finite_grammar, finite_spec, t, 100, fmemo);
			double mass = 0;
			std::map<std::string, Rebag::Bag> bags;
			for(auto& [p,lp2] : enumerate_trees(finite_grammar, finite_spec, 0)) bags.emplace(bagkey(p), ftab.bag_of(p));
			for(auto& [k, B] : bags) {
				const double got = std::exp(Rebag::log_Q(ftab, B, t));
				const double want = Q.count(k) ? Q.at(k) : 0.0;
				mass += got;
				const double err = std::abs(got - want) / std::max(want, 1e-300);
				if(want == 0 ? got != 0 : err > 1e-10) { if(bad++ < 3) CHECK(false, "Q(" << k << " | " << t.parseable() << ") = " << got << " vs " << want); }
				worst = std::max(worst, want > 0 ? err : 0.0); n++;
			}
			worst_mass = std::max(worst_mass, std::abs(mass - 1.0));
		}
		// recursive grammar: trees up to size 4, bags up to size 6 (exact for those bags)
		for(int sz=1; sz<=4; sz++) for(auto& t : trees_of_size(r_grammar, r_spec, 0, sz, rmemo)) {
			auto Q = exact_Q(r_grammar, r_spec, t, 6, rmemo);
			for(auto& [k, q] : Q) {
				// recover a tree with this bag to build a Rebag::Bag (any representative)
				Rebag::Bag B; bool found = false;
				for(int s2=1; s2<=6 and not found; s2++) for(auto& u : trees_of_size(r_grammar, r_spec, 0, s2, rmemo)) if(bagkey(u) == k) { B = rtab.bag_of(u); found = true; break; }
				const double got = std::exp(Rebag::log_Q(rtab, B, t));
				const double err = std::abs(got - q)/q;
				worst = std::max(worst, err); n++;
				if(err > 1e-10 and bad++ < 3) CHECK(false, "rec Q(" << k << " | " << t.parseable() << ") = " << got << " vs " << q);
			}
		}
		note("compared " + str(n) + " (x, B') pairs; worst relative error " + str(worst) + "; finite-grammar Q mass error " + str(worst_mass));
		CHECK(bad == 0, bad << " Q values wrong");
		CHECK(worst_mass < 1e-12, "Q(.|x) does not sum to 1 over the finite grammar's bags: " << worst_mass);
	}

	// ==================================================================================
	// exact independent quantities on the finite model
	auto states = finite_states(); // parseable -> {node, prior (spec), loglik (ref_eval)}
	std::map<std::string, std::vector<std::string>> bag_members;
	for(auto& [k, s] : states) bag_members[bagkey(s.node)].push_back(k);
	auto lpi = [&](const std::string& k, double T) { return states.at(k).prior + states.at(k).loglik/T; };
	auto lZ  = [&](const std::string& bk, double T) { std::vector<double> v; for(auto& m : bag_members.at(bk)) v.push_back(lpi(m,T)); return lse(v); };
	std::map<std::pair<int,int>, std::vector<Node>> fmemo;
	std::map<std::string, std::map<std::string,double>> Qof; // x -> bag -> Q
	for(auto& [k, s] : states) Qof[k] = exact_Q(finite_grammar, finite_spec, s.node, 100, fmemo);
	auto lq = [&](const std::string& a, const std::string& b, double T) { // log q(a->b), exact mode
		const std::string bb = bagkey(states.at(b).node);
		return std::log(Qof.at(a).at(bb)) + lpi(b,T) - lZ(bb,T);
	};

	// ==================================================================================
	section("exact mode: proposal distribution from fixed states, and every fb, vs independent q");
	{
		finite_cfg = RebagConfig{};
		const std::vector<std::string> starts = {"0:s2i(%s);2:sx", "0:plus(%s,%s);2:sx;2:1", "0:ite(%s,%s,%s);1:lt(%s,%s);2:sx;2:2;2:3;2:sx", "0:k", "0:plus(%s,%s);2:2;2:2"};
		int badfb = 0, nfb = 0; double worst_fb = 0;
		for(double T : {1.0, 3.0}) for(auto& xs : starts) {
			RebagFinite hx(finite_grammar.from_parseable(xs));
			hx.compute_posterior(finite_data);
			const std::string xk = hx.get_value().parseable();
			std::map<std::string,double> want; double tot = 0;
			for(auto& [yk, s] : states) {
				if(not Qof.at(xk).count(bagkey(s.node))) continue;
				want[yk] = std::exp(lq(xk, yk, T)); tot += want[yk];
			}
			CHECK(std::abs(tot - 1.0) < 1e-12, "exact q(x->.) does not sum to 1 for " << xs << ": " << tot);
			const size_t N = scaled(300000);
			std::map<std::string,double> f; size_t none = 0;
			for(size_t i=0;i<N;i++) {
				auto p = Proposals::rebag(hx, finite_data, T, RebagConfig::verified());
				if(not p) { none++; continue; }
				const std::string yk = p->first.get_value().parseable();
				f[yk]++;
				if(i < 3000) { // fb against the independent MH correction
					const double ind = lq(xk, yk, T) - lq(yk, xk, T);
					const double err = std::abs(p->second - ind);
					worst_fb = std::max(worst_fb, err); nfb++;
					if(err > 1e-9 and badfb++ < 3) CHECK(false, "fb " << p->second << " vs " << ind << " for " << xk << " -> " << yk);
				}
			}
			CHECK(none == 0, "rebag returned no proposal " << none << " times from " << xs);
			double worst = 0; bool outside = false;
			for(auto& [yk,c] : f) if(not want.count(yk)) outside = true;
			for(auto& [yk, p] : want) {
				if(p < 1e-4) continue;
				const double z = (f[yk]/N - p)/std::sqrt(p*(1-p)/N);
				worst = std::max(worst, std::abs(z));
			}
			note("T=" + str(T) + " from " + xs + ": " + str(want.size()) + " reachable states, worst |z| = " + str(worst));
			CHECK(not outside, "proposed a state with zero exact q from " << xs);
			CHECK(worst < 6.0, "proposal distribution from " << xs << " deviates: worst |z| " << worst);
		}
		note("checked " + str(nfb) + " fb values; worst |error| " + str(worst_fb));
		CHECK(badfb == 0, badfb << " fb values disagree with the independent correction");
	}

	// ==================================================================================
	const int K = 32;
	auto run_mode = [&](const std::string& label, RebagConfig cfg, double T, size_t steps, bool expect_pass) {
		finite_cfg = cfg;
		auto exact = exact_posterior(states, T);
		auto reps = replicate(K, [&](int){
			return run_chain_counts(RebagFinite(finite_grammar.generate()), finite_data, scaled(steps), T);
		});
		if(expect_pass) compare_to_exact(reps, exact, label);
		else {
			const int bad = count_bad_states(reps, exact);
			note(label + ": " + str(bad) + " states deviate (a mutant: SHOULD be > 0)");
			CHECK(bad > 0, label << ": mutant was not detected");
		}
	};

	section("MCMCChain stationarity: exact rebag only, T=1");
	run_mode("rebag exact T=1", RebagConfig{}, 1.0, 40000, true);

	section("MCMCChain stationarity: exact rebag only, T=3");
	run_mode("rebag exact T=3", RebagConfig{}, 3.0, 40000, true);

	section("MCMCChain stationarity: 50/50 mixture with regeneration");
	{ RebagConfig c; c.p_rebag = 0.5; run_mode("rebag+regen T=1", c, 1.0, 60000, true); }

	for(size_t KT : {1, 2, 3}) {
		section("MCMCChain stationarity: sampled tries K=" + str(KT) + " (enum_max=1 forces Tries whenever a bag has >1 tree)");
		RebagConfig c; c.opt.enum_max = 1; c.opt.tries = KT;
		run_mode("rebag tries K=" + str(KT), c, 1.0, 60000, true);
	}

	section("MCMCChain stationarity: fallback to regeneration for bags of size > 3");
	{ RebagConfig c; c.opt.max_bag_size = 3; run_mode("rebag fallback>3", c, 1.0, 60000, true); }

	section("MCMCChain stationarity: likelihood cache on (capacity 1000, and 7 to force clearing)");
	{ RebagConfig c; c.opt.cache_capacity = 1000; run_mode("rebag exact, cache 1000", c, 1.0, 40000, true); }
	{ RebagConfig c; c.opt.cache_capacity = 7; c.p_rebag = 0.5; c.opt.enum_max = 2; c.opt.tries = 2; run_mode("rebag mixed modes, cache 7", c, 3.0, 40000, true); }

	section("likelihood cache: fb identical to the independent correction");
	{
		finite_cfg = RebagConfig{};
		Rebag::Options oc = RebagConfig::verified(); oc.cache_capacity = 50;
		int bad = 0;
		for(auto& xs : {"0:plus(%s,%s);2:sx;2:1", "0:ite(%s,%s,%s);1:lt(%s,%s);2:sx;2:2;2:3;2:sx"}) {
			RebagFinite hx(finite_grammar.from_parseable(xs));
			const std::string xk = hx.get_value().parseable();
			for(size_t i=0;i<scaled(5000);i++) {
				auto p = Proposals::rebag(hx, finite_data, 1.0, oc);
				const std::string yk = p->first.get_value().parseable();
				if(std::abs(p->second - (lq(xk,yk,1.0) - lq(yk,xk,1.0))) > 1e-9) bad++;
			}
		}
		CHECK(bad == 0, bad << " cached fb values disagree with the independent correction");
	}

	// ==================================================================================
	section("LOCAL scope: proposal distribution and every fb vs independent pathwise q");
	{
		// subtrees by (nonterminal, bag), with their total PCFG probability (from the Spec)
		std::map<std::pair<int,std::string>, std::vector<Node>> by_bag;
		std::map<std::pair<int,std::string>, double> Gspec;
		for(int nt=0; nt<3; nt++) for(auto& [t, lp] : enumerate_trees(finite_grammar, finite_spec, nt)) {
			by_bag[{nt, bagkey(t)}].push_back(t); Gspec[{nt, bagkey(t)}] += std::exp(lp);
		}
		auto lpi_n = [&](const Node& n, double T) { return finite_spec.tree_lp(n) + ref_loglik(n, finite_data)/T; };
		// log q_p(a -> b) for a, b agreeing outside path p: choose p (1/|a|), generate bag(b_p), pick b ∝ pi
		auto lq_path = [&](const Node& a, const Path& p, const Node& b, double T) {
			const int N = at(a,p).nt(); const std::string D = bagkey(at(b,p));
			std::vector<double> v; for(auto& t : by_bag.at({N,D})) v.push_back(lpi_n(replaced(a,p,t), T));
			return -std::log((double)a.count()) + std::log(Gspec.at({N,D})) + lpi_n(b,T) - lse(v);
		};
		const std::vector<std::string> starts = {"0:plus(%s,%s);2:sx;2:1", "0:ite(%s,%s,%s);1:lt(%s,%s);2:sx;2:2;2:3;2:sx", "0:s2i(%s);2:sx"};
		int badfb = 0, nfb = 0;
		for(double T : {1.0, 3.0}) for(auto& xs : starts) {
			Node x = finite_grammar.from_parseable(xs);
			RebagFinite hx{Node(x)};
			std::map<std::string,double> want;
			for(auto& p : all_paths(x)) {
				const int N = at(x,p).nt();
				for(auto& [k, ts] : by_bag) { if(k.first != N) continue;
					for(auto& t : ts) { Node y = replaced(x,p,t); want[y.parseable()] += std::exp(lq_path(x,p,y,T)); } }
			}
			double tot = 0; for(auto& [k,v] : want) tot += v;
			CHECK(std::abs(tot-1) < 1e-12, "local exact q does not sum to 1: " << tot);
			Rebag::Options o = RebagConfig::verified(); o.scope = Rebag::Scope::Local;
			const size_t NS = scaled(200000);
			std::map<std::string,double> f; size_t none = 0;
			for(size_t i=0;i<NS;i++) {
				auto pr = Proposals::rebag(hx, finite_data, T, o);
				if(not pr) { none++; continue; }
				const Node& y = pr->first.get_value();
				f[y.parseable()]++;
				if(i < 2000) {
					bool match = false; nfb++;
					for(auto& p : all_paths(x)) {
						if(not (replaced(x, p, at(y,p)) == y)) continue; // x and y must agree outside p
						if(std::abs(pr->second - (lq_path(x,p,y,T) - lq_path(y,p,x,T))) < 1e-9) { match = true; break; }
					}
					if(not match and badfb++ < 3) CHECK(false, "local fb " << pr->second << " matches no path for " << xs << " -> " << y.parseable());
				}
			}
			CHECK(none == 0, "local rebag returned no proposal " << none << " times");
			double worst = 0; bool outside = false;
			for(auto& [k,c] : f) if(not want.count(k)) outside = true;
			for(auto& [k,q] : want) if(q > 1e-4) worst = std::max(worst, std::abs((f[k]/NS - q)/std::sqrt(q*(1-q)/NS)));
			note("T=" + str(T) + " from " + xs + ": " + str(want.size()) + " reachable, worst |z| = " + str(worst));
			CHECK(not outside, "local rebag proposed a state with zero exact q");
			CHECK(worst < 6.0, "local proposal distribution deviates: worst |z| " << worst);
		}
		note("checked " + str(nfb) + " local fb values");
		CHECK(badfb == 0, badfb << " local fb values match no pathwise correction");
	}

	section("MCMCChain stationarity: LOCAL scope (exact; tries; mixed+cache)");
	{ RebagConfig c; c.opt.scope = Rebag::Scope::Local; run_mode("local exact T=1", c, 1.0, 60000, true); }
	{ RebagConfig c; c.opt.scope = Rebag::Scope::Local; c.opt.enum_max = 1; c.opt.tries = 2; run_mode("local tries K=2 T=3", c, 3.0, 60000, true); }
	{ RebagConfig c; c.opt.scope = Rebag::Scope::Local; c.p_rebag = 0.5; c.opt.cache_capacity = 100; c.opt.max_bag_size = 2; run_mode("local+regen, fallback>2, cache", c, 1.0, 60000, true); }

	section("MCMCChain stationarity: library default Options (+cache), global and local, T=1 and 3");
	{ RebagConfig c; c.opt = Rebag::Options{}; c.opt.verify = true; c.opt.cache_capacity = 500; c.p_rebag = 0.5; run_mode("defaults global", c, 1.0, 60000, true); }
	{ RebagConfig c; c.opt = Rebag::Options{}; c.opt.verify = true; c.opt.cache_capacity = 500; c.opt.scope = Rebag::Scope::Local; c.p_rebag = 0.5; run_mode("defaults local T=3", c, 3.0, 60000, true); }

	section("mutants must be detected");
	{ RebagConfig c; c.opt.mutant = Rebag::Mutant::NoZ;        run_mode("MUTANT no Z", c, 1.0, 40000, false); }
	{ RebagConfig c; c.opt.mutant = Rebag::Mutant::NoQ;        run_mode("MUTANT no Q", c, 1.0, 40000, false); }
	{ RebagConfig c; c.opt.mutant = Rebag::Mutant::NoBagPrior; run_mode("MUTANT no bag prior in Q", c, 1.0, 40000, false); }

	// ==================================================================================
	section("recursive grammar, MAX_NODES=5: exact posterior over all trees of size <= 5");
	{
		const auto saved = FleetArgs::MAX_NODES;
		FleetArgs::MAX_NODES = 5;
		std::map<std::string,double> lp;
		for(int n=1;n<=5;n++) for(auto& t : trees_of_size(r_grammar, r_spec, 0, n, rmemo)) {
			double ll = 0; for(auto& d : r_data) ll += std::log(R_ALPHA*(r_eval(t, d.input) == d.output) + (1-R_ALPHA)/NOUT);
			lp[t.parseable()] = r_spec.tree_lp(t) + ll;
		}
		std::vector<double> v; for(auto& [k,x] : lp) v.push_back(x);
		const double z = lse(v);
		std::map<std::string,double> exact; for(auto& [k,x] : lp) exact[k] = std::exp(x - z);
		note(str(exact.size()) + " programs in the support");
		for(int mode=0; mode<3; mode++) {
			r_cfg = RebagConfig{}; r_cfg.opt.max_bag_size = 5;      // never count bags the prior forbids
			if(mode == 1) { r_cfg.p_rebag = 0.5; r_cfg.opt.enum_max = 4; r_cfg.opt.tries = 2; }
			if(mode == 2) { r_cfg.opt.scope = Rebag::Scope::Local; r_cfg.opt.cache_capacity = 5000; }
			auto reps = replicate(K, [&](int){
				Node t; do { t = r_grammar.generate(); } while(t.count() > 5);
				return run_chain_counts(RebagRec(std::move(t)), r_data, scaled(60000));
			});
			compare_to_exact(reps, exact, mode == 0 ? "rec rebag exact" : mode == 1 ? "rec rebag+regen, tries" : "rec local, cache");
		}
		FleetArgs::MAX_NODES = saved;
	}

	return finish();
}
