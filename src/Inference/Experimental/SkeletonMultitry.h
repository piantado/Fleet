#pragma once
// skeleton-multitry: regeneration with a multiple-try choice of the terminals (ARCClaude; generic over the grammar).
//
// Pick a can_resample node uniformly (as Fleet's Proposals::regenerate does), draw only its NON-TERMINAL skeleton
// from the grammar (Fleet's Grammar::generate_nonterminal_skeleton), then take ALL ways of filling the skeleton's
// holes with terminals as the tries of a multiple-try move. Derivation and exactness argument: see propose()
// below; tests/fbcheck.cpp verifies the forward-backward terms by proposal frequencies.
//
// Usage: SkeletonMultitry::propose(&grammar, tree, weight, max_fillings) -> optional (new tree, fb), with Fleet's
// fb convention (fb = log q(new|old) - log q(old|new)).

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "Node.h"

namespace SkeletonMultitry {

/// Optional diagnostics filled in by propose() (tests/accept.cpp).
struct Info {
	bool multitry = false;          // false: the over-the-cap regeneration branch
	size_t fillings_old = 0, fillings_new = 0;
	bool same_skeleton = false;     // K' == K (same non-terminal rules, holes of the same types)
	double logW_old = NaN, logW_new = NaN, lQ_old = NaN, lQ_new = NaN;
};

namespace detail {
	/// Same skeleton: terminals match any terminal of the same type.
	inline bool same_skeleton(const Node& a, const Node& b) {
		if(a.rule->is_terminal() or b.rule->is_terminal()) return a.rule->is_terminal() and b.rule->is_terminal() and a.nt() == b.nt();
		if(a.rule != b.rule) return false;
		for(size_t i=0;i<a.nchildren();i++) if(not same_skeleton(a.child(i), b.child(i))) return false;
		return true;
	}
	/// The terminal rules (no children) of nt, in grammar order.
	template<typename GrammarType>
	std::vector<const Rule*> terminals(const GrammarType* grammar, nonterminal_t nt) {
		std::vector<const Rule*> t;
		for(const auto& r : grammar->rules[nt]) if(r.is_terminal()) t.push_back(&r);
		return t;
	}
	/// log P(nt expands to a terminal) = log sum_{terminal r} p(r)/Z(nt)  (-inf if it has none)
	template<typename GrammarType>
	double log_pterm(const GrammarType* grammar, nonterminal_t nt) {
		double l = -infinity;
		for(const auto& r : grammar->rules[nt]) if(r.is_terminal()) l = logplusexp(l, r.logp - grammar->logZ[nt]);
		return l;
	}
	/// log Q(skeleton of the complete subtree n): non-terminal nodes by p(rule), terminal nodes by Pterm(type).
	/// Equals grammar->log_probability(skeleton) + sum over holes of log_pterm.
	template<typename GrammarType>
	double log_skeleton(const GrammarType* grammar, const Node& n) {
		if(n.rule->is_terminal()) return log_pterm(grammar, n.nt());
		double l = n.rule->logp - grammar->logZ[n.nt()];
		for(size_t i=0;i<n.nchildren();i++) l += log_skeleton(grammar, n.child(i));
		return l;
	}
	/// The terminal nodes of a complete subtree, in preorder.
	inline void leaves(Node& n, std::vector<Node*>& out) {
		if(n.rule->is_terminal()) out.push_back(&n);
		else for(size_t i=0;i<n.nchildren();i++) leaves(n.child(i), out);
	}
	/// Replace every hole (null node) of a skeleton by a placeholder terminal of the hole's type.
	template<typename GrammarType>
	void plug_holes(const GrammarType* grammar, Node& n) {
		for(size_t i=0;i<n.nchildren();i++) {
			if(n.child(i).is_null()) n.set_child(i, grammar->makeNode(terminals(grammar, n.rule->type(i)).at(0)));
			else plug_holes(grammar, n.child(i));
		}
	}
	/// Number of joint terminal assignments to the leaves, or cap+1 if it exceeds cap.
	template<typename GrammarType>
	size_t n_fillings(const GrammarType* grammar, const std::vector<Node*>& holes, size_t cap) {
		size_t k = 1;
		for(Node* h : holes) { k *= terminals(grammar, h->nt()).size(); if(k > cap) return cap+1; }
		return k;
	}
	/// Child-index path from the root to p.
	inline std::vector<size_t> path_of(const Node* p) {
		std::vector<size_t> path;
		for(; p->parent != nullptr; p = p->parent) path.push_back(p->pi);
		std::reverse(path.begin(), path.end());
		return path;
	}
	/// weight(root) for every joint assignment of terminals to holes (odometer order, hole 0 fastest); the
	/// holes are nodes of root's tree, mutated in place and left at the LAST assignment.
	template<typename GrammarType, typename WeightF>
	std::vector<double> enumerate(const GrammarType* grammar, Node& root, const std::vector<Node*>& holes, const WeightF& weight) {
		std::vector<std::vector<const Rule*>> ts; for(Node* h : holes) ts.push_back(terminals(grammar, h->nt()));
		std::vector<size_t> idx(holes.size(), 0);
		for(size_t i=0;i<holes.size();i++) holes[i]->rule = ts[i][0];
		std::vector<double> w;
		while(true) {
			w.push_back(weight(static_cast<const Node&>(root)));
			size_t i = 0;
			for(; i<holes.size(); i++) {
				if(++idx[i] < ts[i].size()) { holes[i]->rule = ts[i][idx[i]]; break; }
				idx[i] = 0; holes[i]->rule = ts[i][0];
			}
			if(i == holes.size()) break;
		}
		return w;
	}
	template<typename GrammarType>
	void set_assignment(const GrammarType* grammar, const std::vector<Node*>& holes, size_t k) {
		for(Node* h : holes) { const auto ts = terminals(grammar, h->nt()); h->rule = ts[k % ts.size()]; k /= ts.size(); }
	}
}

/**
 * @brief Skeleton multiple-try proposal. Like regenerate: a node s is chosen uniformly among the can_resample
 *        nodes and its subtree replaced -- but only the subtree's NON-TERMINAL SKELETON K' is drawn from the
 *        grammar (generate_nonterminal_skeleton). The terminals are then chosen as in multiple-try MCMC, with
 *        ALL ways of filling K''s holes with terminals as the tries: the new tree T' is drawn among them with
 *        probability w(T')/W(K'), where w = exp(weight) and W(K') sums w over every filling of K' at s. The
 *        reverse move's tries are all fillings of the OLD skeleton K at s (which include the current tree T).
 *
 *        Generating a subtree and erasing its terminals gives skeleton K with probability
 *            Q(K) = prod_{non-terminal nodes} p(rule|nt) * prod_{holes} Pterm(nt),   Pterm(nt) = sum_{terminal r} p(r|nt),
 *        so the path (s, K', T') has q = (1/z) Q(K') w(T')/W(K'), and, since (T, T', s) determine K and K',
 *            fb = [-log z + log Q(K') + weight(T') - log W(K')] - [-log z' + log Q(K) + weight(T) - log W(K)]
 *        with z, z' the numbers of can_resample nodes of T and T'. When weight = log of the (tempered) target,
 *        the acceptance ratio is the multiple-try ratio  [z Q(K) W(K')] / [z' Q(K') W(K)].
 *        This is exact for ANY weight function that is finite wherever the target is positive; e.g.
 *        prior + likelihood/T for a chain at temperature T makes it a Gibbs choice of the terminals.
 *
 *        Cost cap: if K or K' has more than max_fillings fillings, the holes of K' are instead filled from the
 *        grammar (each hole's terminal r with probability p(r)/Pterm), which is exactly regenerate, with
 *            fb = [-log z + log P(subtree')] - [-log z' + log P(subtree)].
 *        Which branch is taken depends only on the unordered pair {K, K'}, so the reverse move takes the same
 *        branch, and each branch satisfies detailed balance on its own.
 *
 * @param grammar
 * @param from
 * @param weight - double(const Node& tree): log weight of a complete tree (called on every try)
 * @param max_fillings - the cap above
 * @param where - if non-null, set to the child-index path of s (for tests)
 * @param info - if non-null, filled with diagnostics (branch, filling counts, log W and log Q of both skeletons)
 * @return the proposed tree and fb, or {} if nothing is resampleable or every try has weight -infinity
 */
template<typename GrammarType, typename WeightF>
std::optional<std::pair<Node,double>> propose(GrammarType* grammar, const Node& from, const WeightF& weight,
                                      size_t max_fillings, std::vector<size_t>* where=nullptr, Info* info=nullptr) {
	using namespace detail;
	Node ret = from; // copy
	std::vector<Node*> cand;
	for(auto& n : ret) if(n.can_resample) cand.push_back(&n);
	if(cand.empty()) return {};
	const double z = cand.size();
	const std::vector<size_t> path = path_of(cand[myrandom(cand.size())]);
	if(where) *where = path;
	auto at = [&path](Node& root) -> Node& { Node* p = &root; for(size_t i : path) p = &p->child(i); return *p; };

	const nonterminal_t nt = at(ret).nt();
	Node knew = grammar->generate_nonterminal_skeleton(nt);
	if(knew.is_null()) knew = grammar->makeNode(terminals(grammar, nt).at(0)); // the whole subtree is one hole
	else plug_holes(grammar, knew);

	std::vector<Node*> hold, hnew;
	leaves(at(ret), hold); leaves(knew, hnew);
	const bool multitry = n_fillings(grammar, hold, max_fillings) <= max_fillings and n_fillings(grammar, hnew, max_fillings) <= max_fillings;
	if(info) { *info = Info{}; info->multitry = multitry; info->fillings_old = n_fillings(grammar, hold, max_fillings); info->fillings_new = n_fillings(grammar, hnew, max_fillings); info->same_skeleton = same_skeleton(at(ret), knew); }

	if(not multitry) { // regenerate: fill K''s holes from the grammar
		for(Node* h : hnew) {
			const double u = uniform() * std::exp(log_pterm(grammar, h->nt()));
			double acc = 0.0; const Rule* pick = nullptr;
			for(const Rule* r : terminals(grammar, h->nt())) { pick = r; acc += std::exp(r->logp - grammar->logZ[h->nt()]); if(u < acc) break; }
			h->rule = pick;
		}
		const double lp_old = grammar->log_probability(at(ret));
		at(ret).assign(knew);
		const double lp_new = grammar->log_probability(at(ret));
		const double z_new = ret.sum<double>(+[](const Node& n) { return 1.0*n.can_resample; });
		return std::make_pair(std::move(ret), (-log(z) + lp_new) - (-log(z_new) + lp_old));
	}

	// reverse tries: every filling of the old skeleton K (the current tree is one of them)
	const double w_cur = weight(static_cast<const Node&>(ret));
	const double lq_old = log_skeleton(grammar, at(ret));
	double W_old = -infinity;
	for(double x : enumerate(grammar, ret, hold, weight)) {
		if(std::isnan(x)) return {};
		W_old = logplusexp(W_old, x);
	}

	// forward tries: every filling of the new skeleton K'
	at(ret).assign(knew);
	std::vector<Node*> hs; leaves(at(ret), hs);
	const double lq_new = log_skeleton(grammar, at(ret));
	const std::vector<double> w = enumerate(grammar, ret, hs, weight);
	double W_new = -infinity;
	for(double x : w) { if(std::isnan(x)) return {}; W_new = logplusexp(W_new, x); }
	if(W_new == -infinity) return {}; // no try has positive weight: nothing to propose

	// choose a try with probability w/W
	const double u = uniform(); double acc = 0.0; size_t k = 0;
	for(; k<w.size(); k++) { acc += std::exp(w[k] - W_new); if(u < acc) break; }
	if(k == w.size()) { k = w.size()-1; while(w[k] == -infinity) k--; } // round-off at the top end
	set_assignment(grammar, hs, k);

	if(info) { info->logW_old = W_old; info->logW_new = W_new; info->lQ_old = lq_old; info->lQ_new = lq_new; }
	const double z_new = ret.sum<double>(+[](const Node& n) { return 1.0*n.can_resample; });
	const double fb = (-log(z) + lq_new + w[k] - W_new) - (-log(z_new) + lq_old + w_cur - W_old);
	return std::make_pair(std::move(ret), fb);
}

} // namespace SkeletonMultitry
