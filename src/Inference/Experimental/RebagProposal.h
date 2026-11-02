#pragma once

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RebagProposal.h -- the "rebag" Metropolis-Hastings move.
//
// GLOBAL SCOPE (Options::scope = Scope::Global), from a tree x whose nodes are all can_resample:
//   1. Regenerate a uniformly chosen node of x from the grammar -> T'.
//   2. B' = bag(T'), the multiset of grammar rules used anywhere in T' (terminals included, so every
//      tree with bag B' has |B'| nodes). A(B') = all trees rooted at x's nonterminal whose rule
//      multiset is exactly B'. T' is in A(B').
//   3. Draw y from A(B') with probability pi(y)/Z(B'), pi = prior * likelihood^(1/T), Z(B) = sum_{A(B)} pi.
//   4. Accept with  alpha = min(1,  Q(B|y) Z(B') / [ Q(B'|x) Z(B) ] ),   B = bag(x), where
//        Q(B'|x) = P(step 1 yields bag B') = (1/|x|) sum_{nodes m of x} G(N_m, B' - bag(x) + bag(x_m)),
//        G(N, D) = C(N, D) prod_r p(r)^{D_r}  (PCFG probability of generating, from N, SOME tree with bag D),
//        C(N, D) = number of trees rooted at N with bag exactly D.
//   q(x->y) = Q(B'|x) pi(y)/Z(B'), q(y->x) = Q(B|y) pi(x)/Z(B); pi(x)pi(y) cancels in the MH ratio.
//   Q(B|y) > 0 always (regenerating y's root can produce x).
//
// LOCAL SCOPE (Scope::Local): the same idea restricted to the regenerated subtree. Choose a node n
//   (uniformly), regenerate its subtree s -> s', D = bag(s), D' = bag(s'), N = nt(n). Draw y = x[n <- t],
//   t in A(N, D'), with probability pi(y)/Z_n(D'), Z_n(D) = sum_{t in A(N,D)} pi(x[n <- t]). Accept with
//        alpha = min(1,  (|x|/|y|) G(N,D) Z_n(D') / [ G(N,D') Z_n(D) ] ).
//   (Pathwise in the address n: the kernel is a sum over addresses of kernels that are each reversible,
//   since x and y agree outside n.) Much smaller bags than Global, but primitives cannot move between
//   distant parts of the tree.
//
// MODES. The mode of a step is a function of the UNORDERED pair {bag of x's part, bag of y's part}
// only -- that is what keeps the composite kernel reversible:
//   * Exact    : sizes <= max_bag_size and counts <= enum_max: full enumeration, as above.
//   * Tries    : counts > enum_max (<= sample_max), tries = K > 0. K uniform tries from the new bag
//                (choose ∝ pi), K-1 uniform references from the old bag plus x; Z is replaced by its
//                unbiased estimate C * mean(pi). Exact for any K (an MTM on the space extended by the tries).
//   * Fallback : otherwise, the regenerated tree with the ordinary regeneration fb.
// Trees with any can_resample=false node: no proposal (identity step); rebag only creates
// all-resampleable trees, so that class is closed.
//
// INTEGRATION WITH MCMCChain: call from a hypothesis's  propose_tempered(data, T) const  (MCMCChain calls
// it instead of propose() when it exists). The returned fb = log pi(y) - log pi(x) - log alpha_ratio, so
// MCMCChain's  ratio = pi(y) - pi(x) - fb  is exactly log alpha_ratio.
//
// ASSUMPTIONS: (shared with Proposals::regenerate) Grammar::generate is treated as the untruncated PCFG
// (it conditions on depth < GRAMMAR_MAX_DEPTH); the grammar is context-free over nonterminal_t, as Fleet
// grammars are. pi is computed as the hypothesis' own at_temperature() after setting prior =
// compute_prior() and likelihood = compute_likelihood(data) -- i.e. it assumes compute_posterior is
// Fleet's default (prior + likelihood). Options::verify checks this against compute_posterior.
// Likelihoods must be deterministic functions of (tree, data), as MCMCChain also assumes.
//
// CACHING (Options::cache_capacity > 0). Per thread: (a) likelihoods of evaluated trees, (b) for Global
// scope, each bag's trees with their priors and likelihoods, so revisiting a bag (above all the current
// one, whose Z(B) every step needs) costs no enumeration, no compilation and no evaluation. Values are
// exact (full keys, no hashes); the caches clear themselves when the data's identity (address, element
// pointer, size), the grammar's probabilities, or FleetArgs::MAX_NODES change. If you change data IN
// PLACE (same address and size), or anything else compute_prior/compute_likelihood depend on, call
// Rebag::invalidate_caches() (affects every thread).
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Node.h"
#include "Numerics.h"
#include "Random.h"
#include "Proposers.h"
#include "FleetArgs.h"

namespace Rebag {

	using RuleIndex = uint32_t;

	/**
	 * @brief A multiset of rules: sorted (rule index, count>0) pairs.
	 */
	struct Bag {
		std::vector<std::pair<RuleIndex,uint32_t>> v;

		size_t total() const { size_t n = 0; for(auto& [r,c] : v) n += c; return n; }
		bool empty() const { return v.empty(); }
		bool operator==(const Bag& o) const { return v == o.v; }

		void add(RuleIndex r, uint32_t c=1) {
			auto it = std::lower_bound(v.begin(), v.end(), std::make_pair(r, (uint32_t)0));
			if(it != v.end() and it->first == r) it->second += c;
			else v.insert(it, {r, c});
		}
		/** @brief merge-add (linear time) */
		void add(const Bag& o) {
			std::vector<std::pair<RuleIndex,uint32_t>> out; out.reserve(v.size() + o.v.size());
			size_t i = 0, j = 0;
			while(i < v.size() or j < o.v.size()) {
				if(j == o.v.size() or (i < v.size() and v[i].first < o.v[j].first)) out.push_back(v[i++]);
				else if(i == v.size() or o.v[j].first < v[i].first)              out.push_back(o.v[j++]);
				else { out.push_back({v[i].first, v[i].second + o.v[j].second}); i++; j++; }
			}
			v.swap(out);
		}
		/** @brief this - o (linear time), or nullopt if some count would go negative */
		std::optional<Bag> minus(const Bag& o) const {
			Bag out; out.v.reserve(v.size());
			size_t i = 0, j = 0;
			while(i < v.size()) {
				if(j < o.v.size() and o.v[j].first < v[i].first) return std::nullopt; // o has a rule we lack
				if(j < o.v.size() and o.v[j].first == v[i].first) {
					if(o.v[j].second > v[i].second) return std::nullopt;
					if(v[i].second > o.v[j].second) out.v.push_back({v[i].first, v[i].second - o.v[j].second});
					i++; j++;
				}
				else out.v.push_back(v[i++]);
			}
			if(j < o.v.size()) return std::nullopt;
			return out;
		}
		/** @brief this minus one copy of rule r (which must be present) */
		Bag minus_one(RuleIndex r) const {
			Bag out = *this;
			auto it = std::lower_bound(out.v.begin(), out.v.end(), std::make_pair(r, (uint32_t)0));
			assert(it != out.v.end() and it->first == r);
			if(--it->second == 0) out.v.erase(it);
			return out;
		}
		/** @brief this + a - b, or nullopt if negative */
		std::optional<Bag> plus_minus(const Bag& a, const Bag& b) const { Bag t = *this; t.add(a); return t.minus(b); }

		void append_key(std::string& s) const {
			const uint32_t k = (uint32_t)v.size(); s.append((const char*)&k, sizeof(k)); // length prefix: unambiguous
			for(auto& [r,c] : v) { s.append((const char*)&r, sizeof(r)); s.append((const char*)&c, sizeof(c)); }
		}
	};

	/**
	 * @brief Counting, enumeration and uniform sampling of the trees with a given rule multiset.
	 *
	 *  Trees are built in PREORDER. A partial state is (remaining bag R, stack S of open child slots,
	 *  top = next slot). Filling the top slot of type N with a rule r in R of nonterminal N gives
	 *  (R - r, S - top + reversed(children of r)). Each tree corresponds to exactly one preorder rule
	 *  sequence, so  C(R, S) = [S empty][R empty] + sum_r C(R - r, S')  counts every tree once.
	 *  Memoized on (R, S). Counts are doubles: exact below 2^53, and above that only compared with caps
	 *  or used in ratios. They depend only on the grammar's structure, not its probabilities.
	 */
	template<typename GrammarType>
	class Tables {
	public:
		const GrammarType* grammar = nullptr;
		std::vector<const Rule*> rules;                       // index -> rule
		std::unordered_map<const Rule*, RuleIndex> index;     // rule -> index
		std::vector<std::vector<nonterminal_t>> rev_children; // children types, reversed (for the stack)
		std::unordered_map<std::string,double> memo;
		size_t memo_limit = 4'000'000;                        // clearing the memo never changes results

		explicit Tables(const GrammarType* g) { rebuild(g); }

		bool matches(const GrammarType* g) const {
			if(g != grammar) return false;
			size_t i = 0;
			for(size_t nt=0; nt<GrammarType::N_NTs; nt++)
				for(const auto& r : g->rules[nt]) { if(i >= rules.size() or rules[i] != &r) return false; i++; }
			return i == rules.size();
		}

		void rebuild(const GrammarType* g) {
			grammar = g; rules.clear(); index.clear(); rev_children.clear(); memo.clear();
			for(size_t nt=0; nt<GrammarType::N_NTs; nt++) {
				for(const auto& r : g->rules[nt]) {
					index[&r] = (RuleIndex)rules.size();
					rules.push_back(&r);
					rev_children.emplace_back(r.child_types.rbegin(), r.child_types.rend());
				}
			}
		}

		/** @brief a fingerprint of the grammar's probabilities (cached priors are invalid when it changes) */
		uint64_t probability_fingerprint() const {
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](double d) { uint64_t u; std::memcpy(&u, &d, sizeof u); h = (h ^ u) * 1099511628211ull; };
			for(const Rule* r : rules) { mix(r->logp); mix(grammar->logZ[r->nt]); }
			return h;
		}

		/** @brief exact serialization of a tree: its postorder rule-index sequence (arities make it unique) */
		void append_tree_key(std::string& s, const Node& n) const {
			const uint32_t sz = (uint32_t)n.count(); s.append((const char*)&sz, sizeof sz);
			for(const auto& m : n) { const RuleIndex r = index.at(m.rule); s.append((const char*)&r, sizeof r); }
		}

		Bag bag_of(const Node& n) const {
			Bag b;
			for(const auto& x : n) b.add(index.at(x.rule));
			return b;
		}

		/**
		 * @brief bags of every subtree, in one postorder pass (linear merges instead of one traversal per node)
		 * @return pairs (subtree root, its bag), one per node of n
		 */
		std::vector<std::pair<const Node*, Bag>> subtree_bags(const Node& n) const {
			std::vector<std::pair<const Node*, Bag>> out; out.reserve(n.count());
			sb_rec(n, out);
			return out;
		}

		double log_bag_prior(const Bag& D) const {
			double lp = 0.0;
			for(auto& [r,c] : D.v) lp += c * (rules[r]->logp - grammar->logZ[rules[r]->nt]);
			return lp;
		}

		/** @brief necessary condition: per type, #rules producing it == #slots of it (+1 for the root) */
		bool balanced(const Bag& D, nonterminal_t N) const {
			std::array<long, GrammarType::N_NTs> bal{};
			bal[N] += 1;
			for(auto& [r,c] : D.v) {
				bal[rules[r]->nt] -= c;
				for(auto t : rules[r]->child_types) bal[t] += c;
			}
			for(auto x : bal) if(x != 0) return false;
			return true;
		}

		double count_state(const Bag& R, size_t Rtotal, const std::vector<nonterminal_t>& S) {
			if(S.empty()) return Rtotal == 0 ? 1.0 : 0.0;
			if(Rtotal < S.size()) return 0.0;
			if(S.size() == 1 and Rtotal == 1) { // one slot, one rule left: answer directly (no memo)
				return rules[R.v[0].first]->nt == S[0] and rules[R.v[0].first]->N == 0 ? 1.0 : 0.0;
			}
			std::string key; key.reserve(4 + 8*R.v.size() + 2*S.size());
			R.append_key(key); key.append((const char*)S.data(), S.size()*sizeof(nonterminal_t));
			if(auto it = memo.find(key); it != memo.end()) return it->second;

			const nonterminal_t N = S.back();
			double tot = 0.0;
			for(auto& [ri, c] : R.v) {
				if(rules[ri]->nt != N) continue;
				tot += count_state(R.minus_one(ri), Rtotal-1, push(S, ri));
			}
			if(memo.size() >= memo_limit) memo.clear();
			memo.emplace(std::move(key), tot);
			return tot;
		}

		/** @brief C(N, D): number of trees rooted at N with rule multiset exactly D */
		double count(const Bag& D, nonterminal_t N) {
			if(D.empty() or not balanced(D, N)) return 0.0;
			return count_state(D, D.total(), {N});
		}

		/** @brief log G(N, D) = log [ C(N,D) prod_r p(r)^{D_r} ]  (-inf if no tree) */
		double log_G(const Bag& D, nonterminal_t N) {
			const double c = count(D, N);
			return c > 0 ? std::log(c) + log_bag_prior(D) : -infinity;
		}

		/** @brief all trees rooted at N with bag D (only call when the count is small) */
		std::vector<Node> enumerate(const Bag& D, nonterminal_t N) {
			std::vector<Node> out;
			const double c = count(D,N);
			if(c == 0.0) return out;
			out.reserve((size_t)c);
			std::vector<RuleIndex> seq; seq.reserve(D.total());
			enum_rec(D, D.total(), {N}, seq, out);
			return out;
		}

		/** @brief a uniform draw from the trees rooted at N with bag D (requires count(D,N) > 0) */
		Node sample_uniform(const Bag& D, nonterminal_t N) {
			assert(count(D,N) > 0);
			std::vector<RuleIndex> seq;
			Bag R = D; size_t Rt = D.total(); std::vector<nonterminal_t> S = {N};
			std::vector<std::pair<double,RuleIndex>> opts;
			while(not S.empty()) {
				const nonterminal_t top = S.back();
				double tot = 0.0; opts.clear();
				for(auto& [ri,c] : R.v) {
					if(rules[ri]->nt != top) continue;
					const double w = count_state(R.minus_one(ri), Rt-1, push(S, ri));
					if(w > 0) { opts.push_back({w, ri}); tot += w; }
				}
				assert(not opts.empty());
				double u = uniform() * tot;
				RuleIndex pick = opts.back().second;
				for(auto& [w, ri] : opts) { u -= w; if(u <= 0) { pick = ri; break; } }
				S = push(S, pick);
				R = R.minus_one(pick); Rt--;
				seq.push_back(pick);
			}
			size_t pos = 0;
			return build(seq, pos);
		}

	private:
		const Bag& sb_rec(const Node& n, std::vector<std::pair<const Node*, Bag>>& out) const {
			Bag b; b.add(index.at(n.rule));
			for(size_t i=0;i<n.nchildren();i++) b.add(sb_rec(n.child(i), out)); // copy: out may reallocate
			out.emplace_back(&n, std::move(b));
			return out.back().second;
		}

		std::vector<nonterminal_t> push(const std::vector<nonterminal_t>& S, RuleIndex ri) const {
			std::vector<nonterminal_t> S2; S2.reserve(S.size() - 1 + rev_children[ri].size());
			S2.assign(S.begin(), S.end()-1);
			S2.insert(S2.end(), rev_children[ri].begin(), rev_children[ri].end());
			return S2;
		}

		void enum_rec(const Bag& R, size_t Rt, const std::vector<nonterminal_t>& S, std::vector<RuleIndex>& seq, std::vector<Node>& out) {
			if(S.empty()) { if(Rt == 0) { size_t pos = 0; out.push_back(build(seq, pos)); } return; }
			const nonterminal_t N = S.back();
			for(auto& [ri, c] : R.v) {
				if(rules[ri]->nt != N) continue;
				Bag R2 = R.minus_one(ri);
				auto S2 = push(S, ri);
				if(count_state(R2, Rt-1, S2) == 0.0) continue; // prune dead ends
				seq.push_back(ri);
				enum_rec(R2, Rt-1, S2, seq, out);
				seq.pop_back();
			}
		}

		Node build(const std::vector<RuleIndex>& seq, size_t& pos) const {
			const Rule* r = rules[seq[pos++]];
			Node n = grammar->makeNode(r);
			for(size_t i=0;i<r->N;i++) n.set_child(i, build(seq, pos));
			return n;
		}
	};

	template<typename GrammarType>
	Tables<GrammarType>& tables_for(const GrammarType* g) {
		thread_local std::unordered_map<const GrammarType*, std::unique_ptr<Tables<GrammarType>>> T;
		auto& p = T[g];
		if(not p) p = std::make_unique<Tables<GrammarType>>(g);
		else if(not p->matches(g)) p->rebuild(g);
		return *p;
	}

	/**
	 * @brief log Q(Bp | x): probability that regenerating a uniform node of x gives a tree with bag Bp.
	 *        x must be all-resampleable (so the node choice is uniform over all |x| nodes).
	 */
	template<typename GrammarType>
	double log_Q(Tables<GrammarType>& tab, const Bag& Bp, const Node& x) {
		const auto sb = tab.subtree_bags(x);
		const Bag& Bx = sb.back().second; // the root is last in postorder
		double lq = -infinity;
		for(const auto& [m, bm] : sb) {
			auto D = Bp.plus_minus(bm, Bx); // Bp - (bag(x) - bag(x_m))
			if(not D or D->empty()) continue;
			lq = logplusexp_full(lq, tab.log_G(*D, m->nt()));
		}
		return lq - std::log((double)sb.size());
	}

	enum class Scope { Global, Local };
	enum class Mode { Exact, Tries, Fallback };
	enum class Mutant { None, NoZ, NoQ, NoBagPrior }; // TESTING ONLY: deliberately wrong variants

	struct Options {
		Scope  scope        = Scope::Global;
		size_t max_bag_size = 16;      // larger bags: Fallback (no counting at all; counting large bags costs CPU)
		// Defaults chosen by Testing/RebagBenchmark: small exact bags plus a few tries keep a move at ~2
		// likelihood evaluations, which is what makes rebag competitive per evaluation (large enum_max is
		// stronger per STEP but far costlier). Use cache_capacity > 0 too unless your data change in place.
		double enum_max     = 32;      // Exact when both counts <= enum_max
		size_t tries        = 4;       // > 0: Tries when a count exceeds enum_max (and both <= sample_max); 0: Fallback
		double sample_max   = std::numeric_limits<double>::infinity();
		size_t cache_capacity = 0;     // > 0: cache likelihoods (and, for Global, whole bags); max #trees in each, per thread
		bool   verify       = false;   // check our pi(y) against compute_posterior (one extra evaluation per step)
		Mutant mutant       = Mutant::None;
	};

	struct Stats { // per thread
		unsigned long exact=0, tries=0, fallback=0, identity=0, evaluations=0, lik_hits=0, bag_hits=0;
	};
	inline thread_local Stats stats;

	inline std::atomic<uint64_t> cache_epoch{0};
	/** @brief invalidate every thread's caches (e.g. after changing data in place) */
	inline void invalidate_caches() { cache_epoch++; }

	struct NodeHash { size_t operator()(const Node& n) const { return n.hash(); } };

	struct BagEntry { std::vector<Node> trees; std::vector<double> prior, lik; };

	template<typename HYP>
	struct Caches {
		std::unordered_map<Node, std::pair<double,double>, NodeHash> lik; // tree -> (prior, likelihood)
		std::unordered_map<std::string, BagEntry> bags;
		size_t bag_trees = 0;
		// validity
		uint64_t epoch = UINT64_MAX, gfp = 0; size_t max_nodes = 0;
		const void* data_id = nullptr; const void* elems = nullptr; size_t nelems = 0;
		void clear() { lik.clear(); bags.clear(); bag_trees = 0; }
	};
	template<typename HYP>
	Caches<HYP>& caches_for() { thread_local Caches<HYP> c; return c; }
	/** @brief clear THIS thread's caches for HYP (invalidate_caches() clears every thread's) */
	template<typename HYP>
	void clear_cache() { caches_for<HYP>().clear(); }

	/**
	 * @brief Evaluates trees with ONE scratch hypothesis (no per-candidate copy of the hypothesis; the
	 *        program is compiled only when a likelihood must actually be computed).
	 */
	template<typename HYP, typename GrammarType>
	struct Evaluator {
		using data_t = typename HYP::data_t;
		HYP scratch;
		const data_t& data;
		const Options& opt;
		Caches<HYP>* C = nullptr;

		Evaluator(const HYP& from, const data_t& d, const Options& o, Tables<GrammarType>& tab) : scratch(from), data(d), opt(o) {
			if(opt.cache_capacity == 0) return;
			C = &caches_for<HYP>();
			const void* e = nullptr; size_t ne = 0;
			if constexpr (requires { d.data(); d.size(); }) { e = (const void*)d.data(); ne = d.size(); }
			const uint64_t ep = cache_epoch.load(), fp = tab.probability_fingerprint();
			if(C->epoch != ep or C->gfp != fp or C->max_nodes != FleetArgs::MAX_NODES or
			   C->data_id != (const void*)&d or C->elems != e or C->nelems != ne) {
				C->clear();
				C->epoch = ep; C->gfp = fp; C->max_nodes = FleetArgs::MAX_NODES; C->data_id = &d; C->elems = e; C->nelems = ne;
			}
			// each structure is bounded by cache_capacity (trees); cleared only between calls, so entries stay valid within one
			if(C->lik.size() > opt.cache_capacity or C->bag_trees > opt.cache_capacity) C->clear();
		}

		void set_tree(const Node& t, bool compile) {
			if constexpr (requires(HYP& h, Node&& n) { h.set_value(std::move(n), false); h.compile(); }) {
				scratch.set_value(Node(t), false);
				if(compile) scratch.compile();
			}
			else scratch.set_value(Node(t));
		}

		/** @brief (prior, likelihood) of tree t; likelihood is NaN when the prior is -inf */
		std::pair<double,double> eval(const Node& t) {
			if(C) if(auto it = C->lik.find(t); it != C->lik.end()) { stats.lik_hits++; return it->second; }
			set_tree(t, false);
			const double pr = scratch.compute_prior();
			double ll = NaN;
			if(not (pr == -infinity or std::isnan(pr))) {
				if constexpr (requires(HYP& h) { h.compile(); }) scratch.compile();
				stats.evaluations++;
				ll = scratch.compute_likelihood(data); // no breakout: exact value
			}
			if(C) {
				// the scratch's tree is no longer needed (set_tree overwrites it next time): MOVE it into the
				// cache rather than copying t again
				if constexpr (requires(HYP& h) { { h.get_value() } -> std::same_as<Node&>; })
					C->lik.emplace(std::move(scratch.get_value()), std::make_pair(pr, ll));
				else
					C->lik.emplace(t, std::make_pair(pr, ll));
			}
			return {pr, ll};
		}

		/** @brief the tempered log posterior, computed by the hypothesis' own at_temperature */
		double at_T(double pr, double ll, double T) {
			if(pr == -infinity or std::isnan(pr) or std::isnan(ll)) return -infinity;
			scratch.prior = pr; scratch.likelihood = ll; scratch.posterior = pr + ll;
			const double v = scratch.at_temperature(T);
			return std::isnan(v) ? -infinity : v;
		}

		/** @brief the trees of a bag with their priors and likelihoods (cached for Global scope) */
		const BagEntry& bag(Tables<GrammarType>& tab, const Bag& B, nonterminal_t root, BagEntry& local) {
			std::string key;
			if(C) {
				key = "G"; key.append((const char*)&root, sizeof(root)); B.append_key(key); // 'G'/'L' prefixes keep global and local keys disjoint
				if(auto it = C->bags.find(key); it != C->bags.end()) { stats.bag_hits++; return it->second; }
			}
			BagEntry e;
			e.trees = tab.enumerate(B, root);
			e.prior.reserve(e.trees.size()); e.lik.reserve(e.trees.size());
			for(auto& t : e.trees) { auto [p, l] = eval(t); e.prior.push_back(p); e.lik.push_back(l); }
			if(C) { C->bag_trees += e.trees.size(); return C->bags.emplace(std::move(key), std::move(e)).first->second; }
			local = std::move(e);
			return local;
		}
	};

	/**
	 * @brief LOCAL scope: the subtrees t with bag B under nonterminal `root`, with prior and likelihood
	 *        of the WHOLE tree x[k <- t]. Cached on (x, k, root, B) -- x survives every rejected step,
	 *        so the current subtree's Z_n(D) (needed every step) and revisited bags at the same node
	 *        cost no enumeration, tree building or evaluation.
	 */
	template<typename Ev, typename GrammarType>
	const BagEntry& local_bag(Ev& ev, Tables<GrammarType>& tab, const std::string& xkey, size_t k,
	                          const Bag& B, nonterminal_t root, BagEntry& local, Node& work, Node* slot) {
		std::string key;
		if(ev.C) {
			key = "L"; key += xkey; key.append((const char*)&k, sizeof k); key.append((const char*)&root, sizeof root); B.append_key(key);
			if(auto it = ev.C->bags.find(key); it != ev.C->bags.end()) { stats.bag_hits++; return it->second; }
		}
		BagEntry e;
		e.trees = tab.enumerate(B, root);
		e.prior.reserve(e.trees.size()); e.lik.reserve(e.trees.size());
		// work is a copy of x and slot its k'th node: swap each (small) t into the slot instead of copying x per candidate
		for(auto& t : e.trees) { slot->assign(Node(t)); auto [p, l] = ev.eval(work); e.prior.push_back(p); e.lik.push_back(l); }
		if(ev.C) { ev.C->bag_trees += e.trees.size(); return ev.C->bags.emplace(std::move(key), std::move(e)).first->second; }
		local = std::move(e);
		return local;
	}

	/** @brief index j with probability exp(lw[j] - lse(lw)) (lse given); never returns a -inf entry */
	inline size_t sample_index(const std::vector<double>& lw, double lse) {
		const double u = std::log(uniform()) + lse;
		double acc = -infinity;
		for(size_t j=0;j<lw.size();j++) { acc = logplusexp_full(acc, lw[j]); if(u <= acc and lw[j] > -infinity) return j; }
		size_t j = lw.size(); while(j-- > 0) if(lw[j] > -infinity) return j; // roundoff guard
		assert(false); return 0;
	}

	template<typename HYP>
	HYP with_value(const HYP& from, Node&& n) {
		HYP h = from; // copy so we keep every other field, as LOTHypothesis::propose does
		h.set_value(std::move(n));
		return h;
	}

	inline Node replaced(const Node& x, size_t preorder_index, const Node& s) {
		Node out = x; size_t i = 0;
		for(auto& n : out) if(i++ == preorder_index) { n.assign(Node(s)); return out; }
		assert(false); return out;
	}

} // namespace Rebag


namespace Proposals {

	/**
	 * @brief The rebag proposal. See the top of RebagProposal.h.
	 * @param from  - current hypothesis (needs get_grammar(), get_value(), set_value, compute_prior,
	 *                compute_likelihood, at_temperature)
	 * @param data  - the data the chain targets
	 * @param T     - the chain's likelihood temperature (MCMCChain passes it to propose_tempered)
	 * @return (proposal, fb) for MCMCChain, or nullopt (= identity step)
	 */
	template<typename HYP>
	std::optional<std::pair<HYP,double>> rebag(const HYP& from, const typename HYP::data_t& data, double T,
	                                           const Rebag::Options& opt = {}) {
		using namespace Rebag;
		auto* grammar = from.get_grammar();
		using GrammarType = std::remove_cv_t<std::remove_pointer_t<decltype(grammar)>>;
		const Node& x = from.get_value();

		if(x.is_null() or not __all_resampleable(x)) { stats.identity++; return {}; }

		auto& tab = tables_for(grammar);
		const size_t nx = x.count();

		// ---- choose the node and regenerate (both scopes) ----
		const size_t k = (size_t)(uniform() * nx) % nx; // preorder index, uniform over the (all resampleable) nodes
		const Node* xm = nullptr; { size_t i = 0; for(auto& n : x) if(i++ == k) { xm = &n; break; } }
		const nonterminal_t N = xm->nt();
		Node s_new = grammar->generate(N);
		Node Tp = replaced(x, k, s_new);
		const double lp_new = grammar->log_probability(s_new), lp_old = grammar->log_probability(*xm);
		auto fallback = [&]() {
			stats.fallback++;
			// Proposals::regenerate's correction: [-log|x| + lp(new)] - [-log|T'| + lp(old)]
			const double fb = (-std::log((double)nx) + lp_new) - (-std::log((double)Tp.count()) + lp_old);
			return std::make_optional(std::make_pair(with_value(from, std::move(Tp)), fb));
		};

		// the part being rebagged, and the root it is assembled under
		const bool global = (opt.scope == Scope::Global);
		const nonterminal_t root = global ? x.nt() : N;
		const Bag B  = global ? tab.bag_of(x)  : tab.bag_of(*xm);
		const Bag Bp = global ? tab.bag_of(Tp) : tab.bag_of(s_new);

		// ---- mode: a function of the unordered pair {B, Bp} ONLY ----
		Mode mode = Mode::Fallback;
		double cB = 0, cBp = 0;
		if(B.total() <= opt.max_bag_size and Bp.total() <= opt.max_bag_size) {
			cB  = tab.count(B,  root);
			cBp = tab.count(Bp, root);
			assert(cB > 0 and cBp > 0);
			const double cmax = std::max(cB, cBp);
			if(cmax <= opt.enum_max)                          mode = Mode::Exact;
			else if(opt.tries > 0 and cmax <= opt.sample_max) mode = Mode::Tries;
		}
		if(mode == Mode::Fallback) return fallback();

		Evaluator<HYP, GrammarType> ev(from, data, opt, tab);
		// a candidate part t becomes the whole tree t (global) or x[k <- t] (local). NOTE: k is a POSTORDER
		// index (Fleet's iteration order); only the node it names matters -- the local acceptance is pathwise
		// and uses 1/|x|, 1/|y| (uniform node choice), never the index itself.
		auto whole = [&](const Node& t) { return global ? t : replaced(x, k, t); };

		double lpx, lpy, log_ratio_Z; // log Zhat(new bag) - log Zhat(old bag)
		Node ynode;

		if(mode == Mode::Exact) {
			stats.exact++;
			BagEntry lnew, lold;
			std::vector<double> lw;
			double lZp = -infinity, lZ = -infinity;
			if(global) {
				const BagEntry& en = ev.bag(tab, Bp, root, lnew);
				const BagEntry& eo = ev.bag(tab, B,  root, lold);
				assert(en.trees.size() == (size_t)cBp and eo.trees.size() == (size_t)cB);
				lw.resize(en.trees.size());
				for(size_t j=0;j<lw.size();j++) { lw[j] = ev.at_T(en.prior[j], en.lik[j], T); lZp = logplusexp_full(lZp, lw[j]); }
				if(lZp == -infinity) { stats.identity++; return {}; } // nothing in the new bag is possible: reject
				lpx = -infinity;
				for(size_t j=0;j<eo.trees.size();j++) {
					const double v = ev.at_T(eo.prior[j], eo.lik[j], T);
					lZ = logplusexp_full(lZ, v);
					if(eo.trees[j] == x) lpx = v;
				}
				assert(lpx > -infinity);
				const size_t j = sample_index(lw, lZp);
				ynode = en.trees[j]; lpy = lw[j];
			}
			else {
				std::string xkey; if(ev.C) tab.append_tree_key(xkey, x);
				Node work = x; Node* slot = nullptr; { size_t i = 0; for(auto& n : work) if(i++ == k) { slot = &n; break; } }
				const BagEntry& en = local_bag(ev, tab, xkey, k, Bp, root, lnew, work, slot);
				const BagEntry& eo = local_bag(ev, tab, xkey, k, B,  root, lold, work, slot);
				lw.resize(en.trees.size());
				for(size_t j=0;j<lw.size();j++) { lw[j] = ev.at_T(en.prior[j], en.lik[j], T); lZp = logplusexp_full(lZp, lw[j]); }
				if(lZp == -infinity) { stats.identity++; return {}; }
				lpx = -infinity;
				for(size_t j=0;j<eo.trees.size();j++) {
					const double v = ev.at_T(eo.prior[j], eo.lik[j], T);
					lZ = logplusexp_full(lZ, v);
					if(eo.trees[j] == *xm) lpx = v;
				}
				assert(lpx > -infinity);
				const size_t j = sample_index(lw, lZp);
				ynode = whole(en.trees[j]); lpy = lw[j];
			}
			log_ratio_Z = lZp - lZ;
		}
		else { // Tries
			stats.tries++;
			const size_t K = opt.tries;
			std::vector<Node> ys; std::vector<double> lw;
			double lSf = -infinity;
			for(size_t i=0;i<K;i++) {
				ys.push_back(whole(tab.sample_uniform(Bp, root)));
				auto [p,l] = ev.eval(ys.back()); lw.push_back(ev.at_T(p,l,T));
				lSf = logplusexp_full(lSf, lw.back());
			}
			if(lSf == -infinity) { stats.identity++; return {}; }
			{ auto [p,l] = ev.eval(x); lpx = ev.at_T(p,l,T); }
			double lSr = lpx;
			for(size_t i=0;i+1<K;i++) {
				auto [p,l] = ev.eval(whole(tab.sample_uniform(B, root)));
				lSr = logplusexp_full(lSr, ev.at_T(p,l,T));
			}
			const size_t j = sample_index(lw, lSf);
			ynode = std::move(ys[j]); lpy = lw[j];
			log_ratio_Z = (std::log(cBp) + lSf) - (std::log(cB) + lSr);
		}

		// ---- proposal probabilities of the bags ----
		double lQf, lQr;
		if(global) {
			lQf = log_Q(tab, Bp, x);
			lQr = log_Q(tab, B, ynode);
		}
		else { // choose this address, then generate some tree with this bag
			lQf = -std::log((double)nx)            + tab.log_G(Bp, root);
			lQr = -std::log((double)ynode.count()) + tab.log_G(B,  root);
		}
		assert(lQf > -infinity and lQr > -infinity);

		if(opt.mutant == Mutant::NoZ) log_ratio_Z = 0.0;
		if(opt.mutant == Mutant::NoQ) { lQf = 0.0; lQr = 0.0; }
		if(opt.mutant == Mutant::NoBagPrior) { // counts only in Q (forgetting prod p(r)^D) -- global scope
			auto lQc = [&](const Bag& target, const Node& t) {
				const auto sb = tab.subtree_bags(t); double lq = -infinity;
				for(const auto& [m, bm] : sb) { auto D = target.plus_minus(bm, sb.back().second);
					if(D and not D->empty()) { const double c = tab.count(*D, m->nt()); if(c>0) lq = logplusexp_full(lq, std::log(c)); } }
				return lq; };
			lQf = lQc(Bp, x); lQr = lQc(B, ynode);
		}

		HYP y = with_value(from, std::move(ynode));
		if(opt.verify) { // our pi(y) must be what MCMCChain will compute
			HYP chk = y; chk.compute_posterior(data);
			const double v = (chk.prior == -infinity or std::isnan(chk.posterior)) ? -infinity : chk.at_temperature(T);
			if(std::abs(v - lpy) > 1e-9 * std::max(1.0, std::abs(v)))
				throw std::runtime_error("*** Rebag: pi(y) from compute_prior+compute_likelihood differs from compute_posterior");
		}

		const double log_alpha = lQr - lQf + log_ratio_Z;
		const double fb = lpy - lpx - log_alpha; // so MCMCChain's pi(y) - pi(x) - fb == log_alpha
		return std::make_pair(std::move(y), fb);
	}

} // namespace Proposals
