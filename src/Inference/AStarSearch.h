#pragma once

/**
 * @file AStarSearch.h
 * @brief Exact best-first (A*) search over programs, ordered by posterior score  prior + likelihood / T.
 *
 * SEARCH SPACE
 *   States are partial programs in LEFTMOST-DERIVATION form: the next hole to fill is always the leftmost one. Every
 *   complete program therefore has exactly one derivation, so the search graph is a tree: no duplicate states and no
 *   closed set. Expanding a state fills its leftmost hole with each rule of that hole's type.
 *
 * COSTS (all in nats; lower is better, f = -score)
 *   g(s)  = -log prior of the rules chosen so far        = sum of -log P(r | nt)
 *   h(s)  = sum over open holes of hmin(nt) - UB(s) / T
 *     hmin(nt) = min over complete trees t of type nt of -log P(t), computed exactly by a Knuth/Bellman-Ford fixed point
 *                (the cheapest completion of a hole), and
 *     UB(s)    = an upper bound on the log likelihood of every completion of s: Params::max_loglik (0 is valid
 *                whenever the likelihood is a probability <= 1), optionally tightened by a user LikelihoodBound
 *                evaluated on the partial program.
 *   For a complete program, f is exact: -(prior + likelihood / T), computed by HYP::compute_posterior.
 *
 * GUARANTEES
 *   The prior part of h is admissible and consistent: filling hole nt with rule r changes f by
 *   cost(r) + sum hmin(children) - hmin(nt) >= 0. With a valid UB (an upper bound that never increases along a
 *   derivation), complete programs are popped in exactly non-increasing score order. Ties are broken
 *   deterministically. So the k-th program reported is the k-th best (MAP first), and the search is "anytime".
 *   Partial f values use pathmax (f_child = max(f_child, f_parent)), which keeps a valid bound valid.
 *   Self-checks (verify_admissibility, default on): when a complete program is generated, its exact f must be
 *   >= its parent's f. A violation proves that a bound was wrong: HYP::compute_prior is larger than the grammar
 *   prior, the likelihood exceeds max_loglik, or the user's bound is not admissible. It throws std::logic_error
 *   rather than silently returning a wrong order. Before reporting, each program is re-evaluated, and the
 *   score must reproduce (the likelihood must be deterministic). Limitation: a bound that is too LOW can only be
 *   caught when the search later reaches a completion beneath it. If min_score or the budget prunes that state
 *   first, the programs under it are lost without an error, so give LikelihoodBound only bounds you can prove.
 *
 * MEMORY
 *   A state is 8 bytes: (parent state, rule). The program is recovered by walking the parent chain, so prefixes are
 *   shared. A frontier entry is 16 bytes (f, state, completed-program slot), and each generated complete program
 *   costs 16 bytes until reported. Unreachable states are garbage-collected (mark & compact) as the arena grows.
 *   If the frontier exceeds max_frontier, its worst quarter is dropped. Results then stay exact only for scores
 *   above exact_score_bound(): every program scoring higher than that bound is reported in exact order, and
 *   none is lost.
 *
 * NOTES
 *   - With an exact-match likelihood (0 or -inf), A* reduces to enumerating programs in exact prior order and
 *     testing each one. Its strength is graded likelihoods, or an informative LikelihoodBound that can rule out
 *     partial programs early (e.g., a partial ARC program whose fixed part already determines a wrong output shape).
 *   - The HYP's data_t is typically a std::span: the data must outlive the search.
 *   - HYP must be constructible from Node&& and implement compute_posterior (as LOTHypothesis does). Its grammar
 *     must not be modified during the search.
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern std::atomic<bool> CTRL_C;

template<typename HYP>
class AStarSearch {
public:
	using Grammar_t = typename HYP::Grammar_t;
	using data_t    = typename HYP::data_t;

	static constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
	static constexpr double   INF  = std::numeric_limits<double>::infinity();

	struct Params {
		double   likelihood_temperature = 1.0;  // score = prior + likelihood / T   (T > 0)
		double   max_loglik             = 0.0;  // upper bound on the log likelihood of ANY complete program
		double   min_score              = -INF; // never report or expand anything scoring below this
		size_t   max_frontier           = 50'000'000; // frontier entries before the worst quarter is dropped
		uint64_t max_expansions         = std::numeric_limits<uint64_t>::max();
		bool     verify_admissibility   = true; // throw if a bound is proven invalid, or a score fails to reproduce
		double   tol                    = 1e-9; // relative tolerance for those checks (floating point summation order)
	};

	struct Stats {
		uint64_t expansions = 0, generated = 0, evaluated = 0, yielded = 0;
		uint64_t pruned = 0;   // children with f = inf, or f beyond -min_score
		uint64_t dropped = 0;  // frontier entries dropped by max_frontier
		uint64_t gcs = 0;
		size_t   peak_frontier = 0, arena = 0, live_completes = 0;
	};

	enum class Status { Exhausted, Stopped, Budget, Interrupted };

	/// Upper bound on the log likelihood of every completion of a partial program (null children = holes).
	/// Must be admissible: never below the true log likelihood of any completion. Return -inf to prune.
	using LikelihoodBound = std::function<double(const Node& partial)>;

	/// Called for each complete program, best first. h has prior/likelihood/posterior computed (posterior at T=1).
	/// score = prior + likelihood/T. Return false to stop.
	using Callback = std::function<bool(HYP& h, double score)>;

private:
	struct State    { uint32_t parent; uint32_t rule; };       // root: {NONE, NONE}
	struct Entry    { double f; uint32_t id; uint32_t comp; }; // comp: index into completes_ or NONE
	struct Complete { double prior, likelihood; };
	struct RuleInfo { const Rule* r; double cost; };
	static_assert(sizeof(State) == 8 and sizeof(Entry) == 16);

	Grammar_t*                          grammar_;
	data_t                              data_;
	const Params                        P_;
	LikelihoodBound                     bound_;

	std::vector<RuleInfo>               rules_;
	std::vector<std::vector<uint32_t>>  by_nt_;
	std::vector<double>                 hmin_;
	nonterminal_t                       start_;

	std::vector<State>                  arena_;
	std::vector<Complete>               completes_;
	std::vector<Entry>                  heap_;
	size_t                              next_gc_ = size_t(1) << 22;
	bool                                gc_pending_ = false;

	double                              exact_through_ = INF; // f below this: complete and exactly ordered
	double                              last_f_ = -INF;
	Stats                               st_;

	// scratch
	std::vector<uint32_t>               path_;
	std::vector<nonterminal_t>          holes_;

	// heap order: smallest f on top; ties: completed programs first, then older states first
	static bool worse(const Entry& a, const Entry& b) {
		if(a.f != b.f) return a.f > b.f;
		const bool ac = a.comp != NONE, bc = b.comp != NONE;
		if(ac != bc) return bc;
		return a.id > b.id;
	}
	static bool better(const Entry& a, const Entry& b) { return worse(b, a); }

	double tol(double x) const { return P_.tol * std::max(1.0, std::abs(x)); }

	void push(const Entry& e) {
		heap_.push_back(e);
		std::push_heap(heap_.begin(), heap_.end(), worse);
		st_.peak_frontier = std::max(st_.peak_frontier, heap_.size());
		if(heap_.size() > P_.max_frontier) compact();
	}

	Entry pop() {
		std::pop_heap(heap_.begin(), heap_.end(), worse);
		Entry e = heap_.back();
		heap_.pop_back();
		return e;
	}

	/// rule indices of state id, root to leaf, into path_
	void decode(uint32_t id) {
		path_.clear();
		for(uint32_t s = id; arena_[s].rule != NONE; s = arena_[s].parent) path_.push_back(arena_[s].rule);
		std::reverse(path_.begin(), path_.end());
	}

	/// open holes after applying path_ (top of stack = back = leftmost hole); returns g
	double holes_after_path() {
		holes_.clear();
		holes_.push_back(start_);
		double g = 0.0;
		for(uint32_t ri : path_) {
			const Rule* r = rules_[ri].r;
			if(holes_.empty() or holes_.back() != r->nt) throw std::logic_error("*** AStarSearch: corrupt derivation");
			holes_.pop_back();
			for(size_t k = r->N; k-- > 0; ) holes_.push_back(r->type(k));
			g += rules_[ri].cost;
		}
		return g;
	}

	Node build_rec(const std::vector<uint32_t>& path, size_t& k) const {
		const Rule* r = rules_[path[k++]].r;
		Node n = grammar_->makeNode(r);
		for(size_t i = 0; i < r->N and k < path.size(); i++) n.set_child(i, build_rec(path, k));
		return n;
	}
	/// program (possibly partial; unfilled holes are null) for a rule sequence in leftmost-derivation order
	Node build(const std::vector<uint32_t>& path) const {
		if(path.empty()) return Node();
		size_t k = 0;
		Node n = build_rec(path, k);
		assert(k == path.size());
		return n;
	}

	void evaluate(HYP& h, double& prior, double& lik) {
		++st_.evaluated;
		h.compute_posterior(data_);
		prior = h.prior;
		lik   = h.likelihood;
		if(std::isnan(prior)) prior = -INF;
		if(std::isnan(lik) or prior == -INF) lik = -INF;
	}

	double score_of(double prior, double lik) const {
		if(prior == -INF or lik == -INF) return -INF;
		return prior + lik / P_.likelihood_temperature;
	}

	void expand(const Entry& e) {
		++st_.expansions;
		decode(e.id);
		const double g = holes_after_path();
		if(holes_.empty()) throw std::logic_error("*** AStarSearch: expanding a complete program");

		const nonterminal_t nt = holes_.back();
		double hrest = 0.0;
		for(size_t i = 0; i + 1 < holes_.size(); i++) hrest += hmin_[holes_[i]];
		const bool completes_here = (holes_.size() == 1); // only the hole being filled remains
		const double T = P_.likelihood_temperature;
		const double fmax = -P_.min_score;

		for(uint32_t ri : by_nt_[nt]) {
			const RuleInfo& R = rules_[ri];
			++st_.generated;
			const double g2 = g + R.cost;
			double h2 = hrest;
			for(size_t k = 0; k < R.r->N; k++) h2 += hmin_[R.r->type(k)];
			if(not std::isfinite(g2 + h2)) { ++st_.pruned; continue; } // a hole that can never be completed

			if(completes_here and R.r->N == 0) {
				// ---- complete program: exact f
				path_.push_back(ri);
				HYP hyp(build(path_));
				path_.pop_back();
				double prior, lik;
				evaluate(hyp, prior, lik);
				const double f2 = -score_of(prior, lik);
				if(P_.verify_admissibility and f2 < e.f - tol(e.f)) {
					std::string why;
					if(prior > -g2 + tol(g2)) why = "HYP::compute_prior (" + std::to_string(prior) + ") exceeds the grammar prior (" + std::to_string(-g2) + ")";
					else if(lik > P_.max_loglik + tol(P_.max_loglik)) why = "likelihood " + std::to_string(lik) + " exceeds Params::max_loglik";
					else why = "the LikelihoodBound is not admissible (it underestimated a completion's likelihood)";
					throw std::logic_error("*** AStarSearch: inadmissible bound for " + hyp.string() + ": " + why);
				}
				if(not (f2 <= fmax)) { ++st_.pruned; continue; } // also catches f2 = inf
				arena_.push_back(State{e.id, ri});
				completes_.push_back(Complete{prior, lik});
				push(Entry{f2, uint32_t(arena_.size() - 1), uint32_t(completes_.size() - 1)});
			}
			else {
				// ---- partial program: lower bound
				double ub = P_.max_loglik;
				if(bound_) {
					path_.push_back(ri);
					const double b = bound_(build(path_));
					path_.pop_back();
					if(std::isnan(b)) throw std::logic_error("*** AStarSearch: LikelihoodBound returned NaN");
					ub = std::min(ub, b);
				}
				if(ub == -INF) { ++st_.pruned; continue; }
				double f2 = g2 + h2 - ub / T;
				f2 = std::max(f2, e.f); // pathmax
				if(not (f2 <= fmax)) { ++st_.pruned; continue; }
				if(arena_.size() >= NONE - 1) throw std::length_error("*** AStarSearch: state arena overflow");
				arena_.push_back(State{e.id, ri});
				push(Entry{f2, uint32_t(arena_.size() - 1), NONE});
			}
		}
	}

	/// drop the worst quarter of the frontier; results stay exact for f below the best dropped f
	void compact() {
		const size_t keep = std::max<size_t>(1, P_.max_frontier * 3 / 4);
		if(heap_.size() <= keep) return;
		std::nth_element(heap_.begin(), heap_.begin() + keep, heap_.end(), better);
		const double dropped_best = heap_[keep].f; // nth_element: everything after keep is >= this
		exact_through_ = std::min(exact_through_, dropped_best);
		st_.dropped += heap_.size() - keep;
		heap_.resize(keep);
		std::make_heap(heap_.begin(), heap_.end(), worse);
		gc_pending_ = true; // NOT here: we may be inside expand(), which still refers to its state by id
	}

	/// mark states reachable from the frontier, compact the arena (ids keep their relative order, so ties keep their
	/// order and parents keep smaller ids than children), and compact the completed-program table
	void gc() {
		++st_.gcs;
		const size_t A = arena_.size();
		// operator new never returns null (unlike vector::data() as far as GCC's -Wnull-dereference can tell)
		std::unique_ptr<uint32_t[]> newid(new uint32_t[A + 1]);
		std::fill(newid.get(), newid.get() + A + 1, NONE);
		uint32_t* nid = newid.get();
		nid[0] = 0; // root always live
		for(const Entry& e : heap_) {
			assert(e.id < A);
			for(uint32_t s = e.id; nid[s] == NONE; s = arena_[s].parent) nid[s] = 0; // mark (0 = live, temp)
		}
		uint32_t n = 0;
		for(size_t i = 0; i < A; i++) {
			if(newid[i] == NONE) continue;
			newid[i] = n;
			State s = arena_[i];
			if(s.parent != NONE) s.parent = newid[s.parent]; // parent < i, already renumbered
			arena_[n++] = s;
		}
		arena_.resize(n);
		arena_.shrink_to_fit();

		std::vector<Complete> live;
		live.reserve(heap_.size() / 4 + 1);
		for(Entry& e : heap_) {
			e.id = newid[e.id];
			if(e.comp != NONE) { live.push_back(completes_[e.comp]); e.comp = uint32_t(live.size() - 1); }
		}
		completes_.swap(live);
		completes_.shrink_to_fit();
		next_gc_ = std::max(next_gc_, 2 * arena_.size());
	}

public:
	AStarSearch(const data_t& data, Params p = Params{}, LikelihoodBound bound = {})
		: grammar_(HYP().get_grammar()), data_(data), P_(p), bound_(std::move(bound)) {

		if(not (P_.likelihood_temperature > 0.0)) throw std::invalid_argument("*** AStarSearch: likelihood_temperature must be > 0");
		if(std::isnan(P_.max_loglik) or P_.max_loglik == -INF) throw std::invalid_argument("*** AStarSearch: max_loglik must be a number");
		if(P_.max_loglik == INF) throw std::invalid_argument("*** AStarSearch: max_loglik = +inf gives no bound (use a finite upper bound)");
		if(P_.max_frontier < 4) throw std::invalid_argument("*** AStarSearch: max_frontier too small");

		const size_t NT = grammar_->count_nonterminals();
		start_ = grammar_->start();
		by_nt_.resize(NT);
		for(size_t nt = 0; nt < NT; nt++)
			for(const Rule& r : grammar_->rules[nt]) {
				if(not (r.p > 0.0)) continue;
				const double cost = -(r.logp - grammar_->logZ[nt]); // -log P(r | nt) >= 0
				by_nt_[nt].push_back(uint32_t(rules_.size()));
				rules_.push_back(RuleInfo{&r, cost});
			}

		// hmin(nt): cheapest complete derivation. Knuth's generalization of Dijkstra; with nonnegative costs the
		// Bellman-Ford style sweep converges in at most NT+1 passes.
		hmin_.assign(NT, INF);
		bool changed = true;
		for(size_t sweep = 0; changed; sweep++) {
			if(sweep > NT + 1) throw std::logic_error("*** AStarSearch: hmin did not converge");
			changed = false;
			for(const RuleInfo& R : rules_) {
				double c = R.cost;
				for(size_t k = 0; k < R.r->N; k++) c += hmin_[R.r->type(k)];
				if(c < hmin_[R.r->nt]) { hmin_[R.r->nt] = c; changed = true; }
			}
		}

		arena_.push_back(State{NONE, NONE}); // root: just the start hole
		const double f0 = hmin_[start_] - P_.max_loglik / P_.likelihood_temperature;
		if(std::isfinite(f0)) heap_.push_back(Entry{f0, 0, NONE});
	}

	/**
	 * @brief Report complete programs best first until the callback returns false, the budget is used up, the
	 *        frontier is exhausted (every program scoring >= min_score has been reported), or CTRL-C.
	 *        Can be called again to continue.
	 */
	Status run(const Callback& cb) {
		while(not heap_.empty()) {
			if(CTRL_C) return Status::Interrupted;
			const Entry e = heap_.front();

			if(e.comp == NONE and st_.expansions >= P_.max_expansions) return Status::Budget;
			pop();

			if(P_.verify_admissibility and e.f < last_f_ - tol(last_f_))
				throw std::logic_error("*** AStarSearch: popped f decreased (inconsistent bound)");
			last_f_ = std::max(last_f_, e.f);

			if(e.comp != NONE) {
				decode(e.id);
				HYP hyp(build(path_));
				double prior, lik;
				evaluate(hyp, prior, lik);
				const Complete& c = completes_[e.comp];
				const double s = score_of(prior, lik), s0 = score_of(c.prior, c.likelihood);
				if(P_.verify_admissibility and not (s == s0 or std::abs(s - s0) <= tol(s0)))
					throw std::logic_error("*** AStarSearch: score of " + hyp.string() + " did not reproduce (nondeterministic likelihood?)");
				++st_.yielded;
				if(not cb(hyp, s0)) return Status::Stopped;
			}
			else {
				expand(e);
			}

			if(gc_pending_ or arena_.size() >= next_gc_) { gc(); gc_pending_ = false; } // safe: no state id is held here
		}
		return Status::Exhausted;
	}

	/// Every complete program with score > this bound has been (or will be) reported, in exact order.
	/// It is -inf unless the frontier was truncated by max_frontier.
	double exact_score_bound() const { return -exact_through_; }

	/// Best possible score of anything not yet reported (an upper bound); -inf when exhausted.
	double frontier_bound() const { return heap_.empty() ? -INF : -heap_.front().f; }

	double min_completion_cost(nonterminal_t nt) const { return hmin_.at(nt); }

	const Stats& stats() {
		st_.arena = arena_.size();
		st_.live_completes = completes_.size();
		return st_;
	}
};
