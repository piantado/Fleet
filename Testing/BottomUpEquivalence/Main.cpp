/*
 * Tests for BottomUpEquivalence against an INDEPENDENT brute force:
 *   all trees of each exact integer cost are built directly from the grammar's rules, and every tree is run
 *   through Fleet's own VM (linearize + instruction loop). For every nonterminal and every cost bound C:
 *     (1) the set of BUE classes with level <= C equals the set of distinct value signatures of trees with cost <= C;
 *     (2) each class's level is the minimal cost of any tree with that signature;
 *     (3) each class's representative has that cost, that signature (via the VM), and the maximal log prior
 *         among trees of that signature and cost;
 *   and with targets, the reported programs are exactly the distinct matching signatures, in level order.
 *   Run under several configurations: large cache, tiny cache (forces recomputation), exact verification,
 *   another cost quantum, and a grammar without lazy ops (so error pruning is active).
 */

#include <map>
#include <set>
#include <string>
#include <optional>
#include <random>

#include "Grammar.h"
#include "Singleton.h"
#include "Builtins.h"
#include "BottomUpEquivalence.h"
#include "Fleet.h"

size_t nfail = 0, nchecks = 0;
#define CHECK(cond, ...) do { nchecks++; if(not (cond)) { nfail++; print("FAIL:", #cond, __VA_ARGS__); } } while(0)

// ---------------- Grammar with lazy ops (If/And/Or), three types, errors
class G1 : public Grammar<int,int, int,bool,std::string>, public Singleton<G1> {
public:
	G1() {
		add("x",            Builtins::X<G1>, 2.0);
		add_terminal("1", 1, 1.0);
		add_terminal("2", 2, 0.6);
		add("(%s+%s)",      +[](int a, int b) -> int { return a+b; }, 1.0);
		add("(%s-%s)",      +[](int a, int b) -> int { return a-b; }, 0.7);
		add("(%s*%s)",      +[](int a, int b) -> int { return a*b; }, 0.5);
		add("(%s/%s)",      +[](int a, int b) -> int { if(b==0) throw VMSRuntimeError(); return a/b; }, 0.4);
		add("if(%s,%s,%s)", Builtins::If<G1,int>, 0.3);
		add("len(%s)",      +[](std::string s) -> int { return int(s.size()); }, 0.3);

		add("lt(%s,%s)",    +[](int a, int b) -> bool { return a<b; }, 1.0);
		add("eq(%s,%s)",    +[](int a, int b) -> bool { return a==b; }, 0.5);
		add("and(%s,%s)",   Builtins::And<G1>, 0.4);
		add("or(%s,%s)",    Builtins::Or<G1>, 0.4);
		add("not(%s)",      Builtins::Not<G1>, 0.4);

		add_terminal("''",  std::string(""), 1.0);
		add_terminal("'a'", std::string("a"), 1.0);
		add("cat(%s,%s)",   +[](std::string a, std::string b) -> std::string { return a+b; }, 0.5);
		add("rep(%s,%s)",   +[](std::string a, int k) -> std::string {
			if(k < 0 or k > 3) throw VMSRuntimeError();
			std::string o; for(int i=0;i<k;i++) o += a; return o; }, 0.4);
		add("str(%s)",      +[](int a) -> std::string { return std::to_string(a); }, 0.4);
	}
} g1;

// ---------------- Grammar without lazy ops (error pruning active)
class G2 : public Grammar<int,int, int>, public Singleton<G2> {
public:
	G2() {
		add("x",            Builtins::X<G2>, 2.0);
		add_terminal("1", 1, 1.0);
		add_terminal("3", 3, 0.5);
		add("(%s+%s)",      +[](int a, int b) -> int { return a+b; }, 1.0);
		add("(%s-%s)",      +[](int a, int b) -> int { return a-b; }, 0.8);
		add("(%s*%s)",      +[](int a, int b) -> int { return a*b; }, 0.6);
		add("(%s/%s)",      +[](int a, int b) -> int { if(b==0) throw VMSRuntimeError(); return a/b; }, 0.5);
		add("mod(%s,%s)",     +[](int a, int b) -> int { if(b==0) throw VMSRuntimeError(); return a%b; }, 0.3);
		add("sq(%s)",       +[](int a) -> int { return a*a; }, 0.3);
	}
} g2;

// =====================================================================================================
// Independent machinery
// =====================================================================================================
template<typename G>
uint32_t icost(const G& g, const Rule& r, double q) {
	return uint32_t(std::max(1.0, std::round(-(r.logp - g.logZ[r.nt]) / q)));
}

template<typename G>
struct Brute {
	const G& g; double q; uint32_t C;
	std::map<std::pair<nonterminal_t,uint32_t>, std::vector<Node>> memo;

	const std::vector<Node>& trees(nonterminal_t nt, uint32_t c) {
		auto key = std::make_pair(nt, c);
		if(memo.count(key)) return memo[key];
		std::vector<Node> out;
		for(const Rule& r : g.rules[nt]) {
			const uint32_t ic = icost(g, r, q);
			if(ic > c) continue;
			const uint32_t rem = c - ic;
			if(r.N == 0) { if(rem == 0) out.push_back(g.makeNode(&r)); continue; }
			if(rem < r.N) continue;
			std::vector<uint32_t> lv(r.N);
			std::function<void(size_t,uint32_t)> dist = [&](size_t k, uint32_t left) {
				if(k == r.N-1) { lv[k] = left;
					std::function<void(size_t,Node&)> prod = [&](size_t j, Node& n) {
						if(j == r.N) { out.push_back(n); return; }
						for(const Node& ch : trees(r.child_types[j], lv[j])) { n.set_child(j, Node(ch)); prod(j+1, n); }
					};
					Node n = g.makeNode(&r); prod(0, n); return; }
				for(uint32_t l = 1; l + (r.N-1-k) <= left; l++) { lv[k] = l; dist(k+1, left-l); }
			};
			dist(0, rem);
		}
		return memo[key] = out;
	}
};

// Fleet's VM, driven directly so we can read a value of any type (not only output_t)
template<typename G, typename T>
std::optional<T> vm_eval(const Node& n, int x) {
	using VMS = typename G::VirtualMachineState_t;
	static const typename G::output_t err{};
	VMS vm(x, err, nullptr);
	n.template linearize<VMS,G>(vm.program);
	try {
		while(vm.status == vmstatus_t::GOOD and not vm.program.empty()) {
			auto i = vm.program.top(); vm.program.pop();
			(*reinterpret_cast<typename G::FT*>(i.f))(&vm, i.arg);
		}
	} catch(VMSRuntimeError&) { return std::nullopt; }
	if(vm.status == vmstatus_t::ERROR) return std::nullopt;
	assert(vm.template exactly_one<T>());
	return vm.template getpop<T>();
}

template<typename T> using Sig = std::vector<std::optional<T>>;

template<typename G, typename T>
Sig<T> signature(const Node& n, const std::vector<int>& xs) {
	Sig<T> s; for(int x : xs) s.push_back(vm_eval<G,T>(n, x)); return s;
}

template<typename T>
Sig<T> block_sig(const BUE::Block<T>& b) {
	Sig<T> s; for(size_t i=0;i<b.v.size();i++) s.push_back(((b.err>>i)&1) ? std::nullopt : std::optional<T>(b.v[i])); return s;
}

// check (1)-(3) for nonterminal type T up to bound C
template<typename G, typename T>
void check_type(BottomUpEquivalence<G>& bue, Brute<G>& brute, const std::vector<int>& xs, uint32_t C, const std::string& label) {
	const nonterminal_t nt = G::template nt<T>();
	struct Info { uint32_t mincost; double maxlp; };
	std::map<Sig<T>, Info> truth;
	for(uint32_t c = 1; c <= C; c++)
		for(const Node& n : brute.trees(nt, c)) {
			auto s = signature<G,T>(n, xs);
			double lp = brute.g.log_probability(n);
			auto it = truth.find(s);
			if(it == truth.end()) truth[s] = Info{c, lp};
			else if(it->second.mincost == c) it->second.maxlp = std::max(it->second.maxlp, lp);
		}

	std::set<Sig<T>> seen;
	for(uint32_t idx = 0; idx < bue.num_classes(nt); idx++) {
		const uint32_t lvl = bue.class_level(nt, idx);
		if(lvl > C) break;
		auto [keep, b] = bue.template values<T>(idx);
		auto s = block_sig(*b);
		CHECK(not seen.count(s), label, "duplicate class", idx);
		seen.insert(s);
		auto it = truth.find(s);
		CHECK(it != truth.end(), label, "class not produced by any tree", idx);
		if(it == truth.end()) continue;
		CHECK(it->second.mincost == lvl, label, "level != min cost", lvl, it->second.mincost);
		Node rep = bue.to_node(nt, idx);
		CHECK((signature<G,T>(rep, xs) == s), label, "representative's VM values differ", rep.string());
		uint32_t repcost = 0; for(auto& nn : rep) repcost += icost(brute.g, *nn.rule, brute.q);
		CHECK(repcost == lvl, label, "representative cost", repcost, lvl);
		CHECK(std::abs(brute.g.log_probability(rep) - it->second.maxlp) < 1e-4, label, "representative not max prior", rep.string());
	}
	CHECK(seen.size() == truth.size(), label, "class count", seen.size(), truth.size());
}

// targets: reported == distinct matching int signatures, in nondecreasing level, first = min cost
template<typename G>
void check_targets(const G& g, Brute<G>& brute, const std::vector<int>& xs, const std::vector<std::optional<int>>& tg,
                   uint32_t C, typename BottomUpEquivalence<G>::Params P, const std::string& label) {
	std::map<Sig<int>, uint32_t> truth;
	for(uint32_t c = 1; c <= C; c++)
		for(const Node& n : brute.trees(G::template nt<int>(), c)) {
			auto s = signature<G,int>(n, xs);
			bool ok = true;
			for(size_t i=0;i<xs.size();i++) if(tg[i] and s[i] != tg[i]) ok = false;
			if(ok and not truth.count(s)) truth[s] = c;
		}
	BottomUpEquivalence<G> bue(&g, xs, P);
	bue.set_targets(tg);
	std::set<Sig<int>> rep; uint32_t lastlvl = 0;
	bue.run(C, [&](const Node& n, double lp, uint32_t lvl, const std::vector<int>& outs, uint64_t err) {
		Sig<int> s; for(size_t i=0;i<outs.size();i++) s.push_back(((err>>i)&1) ? std::nullopt : std::optional<int>(outs[i]));
		CHECK(not rep.count(s), label, "reported twice");
		rep.insert(s);
		CHECK(lvl >= lastlvl, label, "levels out of order"); lastlvl = lvl;
		CHECK(truth.count(s) and truth[s] == lvl, label, "reported level != min cost", n.string());
		return true;
	});
	CHECK(rep.size() == truth.size(), label, "reported count", rep.size(), truth.size());
}

uint32_t C1 = 10, C2 = 11;
int main(int argc, char** argv) {
	if(getenv("C1")) C1 = atoi(getenv("C1"));
	if(getenv("C2")) C2 = atoi(getenv("C2"));
	Fleet fleet("BottomUpEquivalence tests");
	fleet.initialize(argc, argv);

	const std::vector<int> xs = {-2, 0, 3, 5};

	struct Cfg { std::string name; double q; size_t budget; bool verify; };
	std::vector<Cfg> cfgs = {
		{"default",      0.5, size_t(1)<<30, false},
		{"tiny-cache",   0.5, 600,           false},
		{"verify",       0.5, 2000,          true },
		{"quantum-0.25", 0.25,size_t(1)<<30, false},
	};

	for(auto& cfg : cfgs) {
		// ---- G1 (lazy ops; no targets => no pruning)
		{
			typename BottomUpEquivalence<G1>::Params P; P.cost_quantum = cfg.q; P.value_budget_bytes = cfg.budget; P.verify_equivalence = cfg.verify;
			const uint32_t C = cfg.q < 0.5 ? C1*2 : C1;
			Brute<G1> brute{g1, cfg.q, C, {}};
			BottomUpEquivalence<G1> bue(&g1, xs, P);
			bue.run(C, [](auto&&...) { return true; });
			check_type<G1,int>(bue, brute, xs, C, "G1/"+cfg.name+"/int");
			check_type<G1,bool>(bue, brute, xs, C, "G1/"+cfg.name+"/bool");
			check_type<G1,std::string>(bue, brute, xs, C, "G1/"+cfg.name+"/string");
			print(cfg.name, "G1 classes", bue.stats().classes, "candidates", bue.stats().candidates,
			      "recomputes", bue.stats().recomputes, "evictions", bue.stats().evictions, "cache bytes", bue.stats().cache_bytes);

			// targets: x*x-1 on first three inputs, unknown on last
			check_targets(g1, brute, xs, {3, -1, 8, std::nullopt}, C, P, "G1/"+cfg.name+"/targets");
			check_targets(g1, brute, xs, {0, 0, 0, 0}, C, P, "G1/"+cfg.name+"/targets0");
		}
		// ---- G2 (no lazy ops => error pruning active with targets)
		{
			typename BottomUpEquivalence<G2>::Params P; P.cost_quantum = cfg.q; P.value_budget_bytes = cfg.budget; P.verify_equivalence = cfg.verify;
			const uint32_t C = cfg.q < 0.5 ? C2*2 : C2;
			Brute<G2> brute{g2, cfg.q, C, {}};
			BottomUpEquivalence<G2> bue(&g2, xs, P);
			bue.run(C, [](auto&&...) { return true; });
			check_type<G2,int>(bue, brute, xs, C, "G2/"+cfg.name+"/int");
			check_targets(g2, brute, xs, {3, -1, 8, std::nullopt}, C, P, "G2/"+cfg.name+"/targets");
			check_targets(g2, brute, xs, {-1, std::nullopt, 1, 1}, C, P, "G2/"+cfg.name+"/targets2");
			P.prune_errors = false;
			check_targets(g2, brute, xs, {-1, std::nullopt, 1, 1}, C, P, "G2/"+cfg.name+"/targets2-noprune");
		}
	}

	// incremental run() must equal one-shot run()
	{
		BottomUpEquivalence<G1> a(&g1, xs), b(&g1, xs);
		a.run(10, [](auto&&...) { return true; });
		for(uint32_t c = 1; c <= 10; c++) b.run(c, [](auto&&...) { return true; });
		for(nonterminal_t nt = 0; nt < 3; nt++) CHECK(a.num_classes(nt) == b.num_classes(nt), "incremental", nt);
	}
	// early stop: callback false
	{
		BottomUpEquivalence<G2> bue(&g2, xs);
		bue.set_targets({3, -1, 8, 24});
		int n = 0;
		bool done = bue.run(30, [&](auto&&...) { n++; return false; });
		CHECK(not done and n == 1, "early stop", n);
		CHECK(not bue.run(31, [](auto&&...) { return true; }), "stopped stays stopped");
	}
	// unsupported op must be rejected
	{
		class G3 : public Grammar<int,int, int,bool> {
		public: G3() { add("x", Builtins::X<G3>); add("flip", Builtins::Flip<G3>); add_terminal("1", 1); }
		} g3;
		bool threw = false;
		try { BottomUpEquivalence<G3> bue(&g3, xs); } catch(std::invalid_argument&) { threw = true; }
		CHECK(threw, "unsupported op not rejected");
	}

	// hasher: (a) single-word changes can never collide (all positions/lanes, both code paths: word() and bytes());
	//         (b) no 96-bit collisions among many random and highly structured (sparse) inputs
	{
		std::mt19937_64 rng(7);
		size_t bad = 0;
		for(int trial = 0; trial < 2000; trial++) {
			size_t n = 1 + rng() % 40;
			std::vector<uint64_t> w(n); for(auto& x : w) x = (trial % 2) ? rng() : (rng() % 3);
			auto H = [&](const std::vector<uint64_t>& v, bool viabytes) {
				BUE::Hasher h; h.word(7); // offset by one word so bytes() exercises the lane-alignment prefix
				if(viabytes) h.bytes(v.data(), v.size()*8); else for(auto x : v) h.word(x);
				return h.finish(); };
			auto base = H(w, true);
			CHECK((H(w, false) == base), "word() and bytes() disagree", trial);
			for(size_t j = 0; j < n; j++) {
				auto v = w; v[j] ^= (uint64_t(1) << (rng() % 64));
				if(H(v, true).a == base.a) bad++;
			}
		}
		CHECK(bad == 0, "single-word change collided", bad);

		// no collisions among distinct sparse, grid-like vectors (exact comparison of distinct counts)
		std::mt19937_64 r3(99);
		std::set<std::vector<int>> distinct;
		std::set<std::pair<uint64_t,uint32_t>> seen2;
		for(int k = 0; k < 300000; k++) {
			std::vector<int> g(100, 0);
			for(int m = 0, M = 1 + int(r3() % 4); m < M; m++) g[r3() % 100] = 1 + int(r3() % 9);
			BUE::Hasher h; BUE::Feed<std::vector<int>>::apply(h, g);
			auto f = h.finish();
			distinct.insert(g); seen2.insert({f.a, f.b});
		}
		CHECK(distinct.size() == seen2.size(), "hash collision among distinct sparse vectors", distinct.size(), seen2.size());
	}

	print(nfail == 0 ? "ALL TESTS PASSED" : "FAILURES", nchecks, "checks,", nfail, "failures");
	return nfail != 0;
}
