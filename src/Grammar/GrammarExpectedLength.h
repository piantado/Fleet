#pragma once

/**
 * @file GrammarExpectedLength.h
 * @brief Exact (non-sampling) properties of the tree distribution defined by any Fleet Grammar.
 *
 * | Function | What it does |
 * |---|---|
 * | `expected_length(g[, start][, cost])` / `analyze(...)` | Expected size of a generated tree (+inf if not finite), plus the spectral radius that decides finiteness. |
 * | `set_max_entropy_probabilities(g, L[, start][, cost])` | Sets rule probabilities to the **maximum-entropy distribution over programs** with expected size `L`. Modifies `g`. |
 * | `assigns_probability_to_infinite_trees(g[, start])` / `analyze_tightness(...)` | Does the grammar put positive probability on infinite trees? Says why, and gives P(infinite). |
 *
 * ```cpp
 * #include "EigenLib.h"
 * #include "Grammar.h"
 * #include "GrammarExpectedLength.h"
 * using namespace GrammarExpectedLength;
 *
 * double E      = expected_length(grammar);                          // E[Node::count()] from nt<output_t>()
 * double leaves = expected_length(grammar, grammar.nt<bool>(), TerminalCost{}); // E[Node::count_terminals()] from bool
 * MaxEntResult m = set_max_entropy_probabilities(grammar, 8.0);      // afterwards E[Node::count()] == 8
 * bool bad      = assigns_probability_to_infinite_trees(grammar);    // P(infinite tree) > 0 ?
 * ```
 *
 * "Length" is the number of nodes (= rule expansions), i.e. `Node::count()`. Any per-rule cost can be used
 * instead through a functor `double(const Rule&)`; `UnitCost`, `TerminalCost`, `InternalCost` are provided.
 *
 * ## Conventions shared by all functions
 * * `start` defaults to the grammar's output type, as in `Grammar::generate()`.
 * * Rules with `p == 0` are never sampled, so they are ignored (and `set_max_entropy_probabilities` keeps them at 0).
 * * Only nonterminals **reachable from `start`** through p > 0 rules matter. Unreachable parts of the grammar,
 *   however badly behaved, do not affect any answer. Per-nonterminal result vectors hold NaN for them.
 * * A reachable nonterminal with no p > 0 rule throws `std::runtime_error` (`generate()` would fail there).
 *   Malformed input (NaN/negative probability, bad `start`, bad cost) throws `std::invalid_argument`.
 * * Normalizers Z_i are recomputed from the rules, not read from the cached `Grammar::Z`.
 * * All of this describes the ideal PCFG. `Grammar::generate()` rejects trees deeper than `GRAMMAR_MAX_DEPTH`
 *   (default 64) and retries, so its empirical mean is slightly smaller near criticality. A subclass that
 *   overrides `sample_rule` with a different distribution is not described by this code.
 *
 * ## The one central object: the mean matrix M
 * Generating a tree is a multitype branching process whose types are nonterminals. With normalized rule
 * probabilities p_r,
 *
 *   M(i,j) = E[number of type-j children when expanding i] = Σ_{r∈R_i} p_r · #{children of r of type j}.
 *
 * Its spectral radius ρ(M) (largest |eigenvalue|; for a nonnegative matrix, the Perron root) is the
 * long-run growth factor of the expected number of nodes per generation:
 *
 * | ρ (over what start reaches) | trees | E[size] |
 * |---|---|---|
 * | < 1 (subcritical)   | finite with prob. 1 | finite |
 * | = 1 (critical)      | finite with prob. 1 | ∞ |
 * | > 1 (supercritical) | infinite with positive prob. | ∞ |
 *
 * Example: `S → f(S,S) [q] | x [1−q]` has ρ = 2q and E = 1/(1−2q). At q = ½ every tree is finite but
 * the mean size is infinite, which is why "infinite expected length" and "infinite trees" are separate
 * questions. (For tightness ρ is examined per strongly connected component; see analyze_tightness.)
 *
 * ## Floating-point limits (by design, the functions refuse rather than guess)
 * * ρ = 1 cannot be told apart from 1 ± 1e-16 with double probabilities. Within `critical_tolerance`
 *   (default 1e-10) of 1 counts as critical: infinite length, but tight (no infinite trees); tightness
 *   then sets `near_critical = true`.
 * * Near criticality E ≈ 1/(1−ρ), so the relative error of E is about L² × 1e-16 from storing p as doubles.
 *   `set_max_entropy_probabilities` meets `rel_tol` or throws. Measured on a RationalRules grammar:
 *   L ≤ 1e3 works at 1e-9; 1e4–1e5 need 1e-6; 1e6 needs 1e-3.
 *
 * ## Tests
 * `Testing/GrammarExpectedLength`, `Testing/GrammarMaxEntropy`, `Testing/GrammarTightness` (`make test`; also
 * `make sanitize`). They check closed forms, brute-force optimization, independent fixed-point iterations,
 * Monte Carlo through `Grammar::generate()`, the Gibbs identity via `Grammar::log_probability()`,
 * optimality against random PCFGs, and all error paths.
 */

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <algorithm>

#include "EigenLib.h"   // must precede Grammar.h if Grammar.h has not been included yet (AM_I_USING_EIGEN)
#include "Grammar.h"

namespace GrammarExpectedLength {

	// =========================================================================================
	//  Costs: what "length" means. A cost functor maps a Rule to a number, and the cost of a
	//  tree is the sum over its nodes. The default (1 per node) gives Node::count().
	// =========================================================================================

	/// The constraint keeps an integer start symbol, e.g. analyze(g, 0), from being taken as a cost functor.
	template<typename F>
	concept RuleCost = std::is_invocable_r_v<double, const F&, const Rule&>;

	struct UnitCost     { double operator()(const Rule&)   const { return 1.0; } };                          ///< Node::count()
	struct TerminalCost { double operator()(const Rule& r) const { return r.is_terminal() ? 1.0 : 0.0; } };  ///< Node::count_terminals()
	struct InternalCost { double operator()(const Rule& r) const { return r.is_terminal() ? 0.0 : 1.0; } };  ///< non-leaf nodes

	// NaN and infinity below are Fleet's own constants (Numerics.h).

	// =========================================================================================
	//  Shared building blocks (internal)
	// =========================================================================================
	namespace detail {

		/// One usable (p > 0) rule of the local grammar below.
		struct LocalRule {
			const Rule*         rule;   // the grammar's rule (for its cost)
			size_t              index;  // its position in grammar.rules[nt], for writing probabilities back
			double              p;      // normalized probability p_r / Z
			std::vector<size_t> kids;   // child nonterminals, as LOCAL indices
		};

		/// The part of a grammar that trees from `start` can use: nonterminals reachable from start
		/// through p > 0 rules, renumbered 0..size()-1 in the order they are found (start is 0).
		/// Every function in this file works on this compact view.
		struct LocalGrammar {
			std::vector<size_t>                 nt;     // nt[a]  = original nonterminal of local index a
			std::vector<size_t>                 local;  // local[i] = local index of nonterminal i, or NONE if unreachable
			std::vector<std::vector<LocalRule>> rules;  // rules[a] = usable rules of local nonterminal a
			static constexpr size_t NONE = std::numeric_limits<size_t>::max();
			size_t size() const { return nt.size(); }
		};

		/// Validate the grammar and build the LocalGrammar from `start` (breadth-first search).
		template<typename G>
		LocalGrammar make_local(const G& grammar, const nonterminal_t start) {
			constexpr size_t N = G::N_NTs;
			if(static_cast<size_t>(start) >= N)
				throw std::invalid_argument("*** start nonterminal " + std::to_string(start) + " out of range (N_NTs=" + std::to_string(N) + ")");

			// Every rule must be well formed, reachable or not.
			for(size_t i=0;i<N;i++) {
				for(const Rule& r : grammar.rules[i]) {
					if(static_cast<size_t>(r.nt) != i)
						throw std::invalid_argument("*** Rule stored under nonterminal " + std::to_string(i) + " has nt=" + std::to_string(r.nt) + ": " + r.string());
					if(not std::isfinite(r.p) or r.p < 0.0)
						throw std::invalid_argument("*** Rule probability must be finite and >= 0: " + r.string());
					for(const nonterminal_t t : r.get_child_types())
						if(static_cast<size_t>(t) >= N) throw std::invalid_argument("*** Child type out of range in rule " + r.string());
				}
			}

			LocalGrammar L;
			L.local.assign(N, LocalGrammar::NONE);
			L.nt.push_back(start);
			L.local[start] = 0;

			// L.nt doubles as the BFS queue: newly found nonterminals are appended and visited later.
			for(size_t a=0; a<L.nt.size(); a++) {
				const auto& rules = grammar.rules[L.nt[a]];

				double Z = 0.0;
				for(const Rule& r : rules) Z += r.p;
				if(not (Z > 0.0))
					throw std::runtime_error("*** Nonterminal " + std::to_string(L.nt[a]) + " is reachable from start " + std::to_string(start) +
					                         " but has no rule with positive probability; trees from start cannot be generated.");
				if(not std::isfinite(Z))
					throw std::invalid_argument("*** Sum of rule probabilities overflowed for nonterminal " + std::to_string(L.nt[a]));

				L.rules.emplace_back();
				for(size_t k=0;k<rules.size();k++) {
					const Rule& r = rules[k];
					if(r.p == 0.0) continue; // never sampled
					LocalRule lr{&r, k, r.p / Z, {}};
					for(const nonterminal_t t : r.get_child_types()) {
						if(L.local[t] == LocalGrammar::NONE) { L.local[t] = L.nt.size(); L.nt.push_back(t); } // newly reached
						lr.kids.push_back(L.local[t]);
					}
					L.rules[a].push_back(std::move(lr));
				}
			}
			return L;
		}

		/// Mean matrix M(a,b) = expected number of b-children when expanding a.
		inline Eigen::MatrixXd mean_matrix(const LocalGrammar& L) {
			Eigen::MatrixXd M = Eigen::MatrixXd::Zero(L.size(), L.size());
			for(size_t a=0;a<L.size();a++)
				for(const LocalRule& r : L.rules[a])
					for(size_t b : r.kids) M(a,b) += r.p;
			return M;
		}

		/// Spectral radius (largest |eigenvalue|) of a nonnegative matrix.
		inline double spectral_radius(const Eigen::MatrixXd& M) {
			if(M.size() == 0) return 0.0;
			Eigen::EigenSolver<Eigen::MatrixXd> es(M, /*computeEigenvectors=*/false);
			if(es.info() != Eigen::Success) throw std::runtime_error("*** Eigenvalue computation failed.");
			double rho = 0.0;
			for(Eigen::Index k=0;k<es.eigenvalues().size();k++) rho = std::max(rho, std::abs(es.eigenvalues()(k)));
			// Sanity check: for a nonnegative matrix, rho <= largest row sum (Perron-Frobenius).
			if(not std::isfinite(rho) or rho > M.rowwise().sum().maxCoeff() * (1.0 + 1e-9) + 1e-12)
				throw std::runtime_error("*** Spectral radius " + std::to_string(rho) + " exceeds the largest row sum.");
			return rho;
		}

		/// Expected tree cost from every local nonterminal.
		///   x(a) = c(a) + Σ_b M(a,b) x(b),  i.e.  x = (I - M)^{-1} c,  with c(a) = expected cost of ONE expansion of a.
		/// This has a finite (and nonnegative) solution iff rho(M) < 1; otherwise `finite` is false.
		struct Expectation { bool finite; double rho; Eigen::VectorXd x; };

		template<typename CostF>
		Expectation expected_cost(const LocalGrammar& L, const CostF& cost, const double critical_tolerance) {
			const Eigen::Index K = L.size();
			const Eigen::MatrixXd M = mean_matrix(L);
			Eigen::VectorXd c = Eigen::VectorXd::Zero(K);
			for(Eigen::Index a=0;a<K;a++) {
				for(const LocalRule& r : L.rules[a]) {
					const double w = static_cast<double>(cost(*r.rule));
					if(not std::isfinite(w) or w < 0.0)
						throw std::invalid_argument("*** Cost must be finite and >= 0 for rule " + r.rule->string());
					c(a) += r.p * w;
				}
			}

			const double rho = spectral_radius(M);
			if(rho >= 1.0 - critical_tolerance) return {false, rho, {}}; // (near-)critical or supercritical: infinite

			// Solve (I - M) x = c with full-pivot LU plus one round of iterative refinement.
			const Eigen::MatrixXd A = Eigen::MatrixXd::Identity(K,K) - M;
			const Eigen::FullPivLU<Eigen::MatrixXd> lu(A);
			if(not lu.isInvertible()) throw std::runtime_error("*** (I - M) is numerically singular although rho < 1.");
			Eigen::VectorXd x = lu.solve(c);
			x += lu.solve(c - A*x);

			// Verify. The exact solution satisfies x = c + Mx >= c >= 0, and the residual must be tiny.
			const double scale = std::max(1.0, x.lpNorm<Eigen::Infinity>());
			if(not x.allFinite() or (c - A*x).lpNorm<Eigen::Infinity>() > 1e-9 * std::max(1e-300, c.lpNorm<Eigen::Infinity>()) or
			   ((x - c).array() < -1e-9 * scale).any())
				throw std::runtime_error("*** Expected-length linear solve failed its checks (rho = " + std::to_string(rho) + ").");
			return {true, rho, x.cwiseMax(c)}; // clamp rounding below the exact lower bound x >= c
		}

		/// Least fixed point of the polynomial system  x(a) = Σ_{r ∈ rules[a]} w[a][r] · Π_{b ∈ kids(r)} x(b),
		/// with nonnegative weights w. Two uses:
		///   - partition functions (max entropy): w = rule weights, x = Z;
		///   - termination probabilities (tightness): w = rule probabilities, x = q = P(tree is finite).
		/// Newton's method started at 0 increases monotonically to the least fixed point (Esparza, Kiefer &
		/// Luttenberger 2010). If a Newton step is unusable (I - J singular near criticality) we take a plain
		/// iteration step x <- F(x) instead, which is also monotone. Callers verify the result themselves.
		struct FixedPoint { Eigen::VectorXd x; bool converged; };

		inline FixedPoint least_fixed_point(const LocalGrammar& L, const std::vector<std::vector<double>>& w, const int max_iterations) {
			const Eigen::Index K = L.size();
			Eigen::VectorXd x = Eigen::VectorXd::Zero(K);
			double previous_size = infinity; // size of the previous step
			for(int it=0; it<max_iterations; it++) {
				// F(x) and its Jacobian J = dF/dx. For one rule term w·x_b1·x_b2·..., d/dx_bl = w·Π_{k≠l} x_bk.
				Eigen::VectorXd F = Eigen::VectorXd::Zero(K);
				Eigen::MatrixXd J = Eigen::MatrixXd::Zero(K, K);
				for(Eigen::Index a=0;a<K;a++) {
					for(size_t k=0;k<L.rules[a].size();k++) {
						const auto& kids = L.rules[a][k].kids;
						double term = w[a][k];
						for(size_t b : kids) term *= x(b);
						F(a) += term;
						for(size_t l=0;l<kids.size();l++) {
							double d = w[a][k];
							for(size_t m=0;m<kids.size();m++) if(m != l) d *= x(kids[m]);
							J(a, kids[l]) += d;
						}
					}
				}

				// Newton step solves (I - J) step = F(x) - x.
				Eigen::VectorXd step = F - x;
				Eigen::FullPivLU<Eigen::MatrixXd> lu(Eigen::MatrixXd::Identity(K,K) - J);
				lu.setThreshold(1e-300); // near criticality I - J is legitimately almost singular; still try Newton
				if(lu.isInvertible()) {
					const Eigen::VectorXd newton = lu.solve(F - x);
					if(newton.allFinite() and newton.minCoeff() >= -1e-12 * (1.0 + x.lpNorm<Eigen::Infinity>())) step = newton;
				}

				x += step;
				if(not x.allFinite() or x.maxCoeff() > 1e150) return {x, false}; // diverging: no finite fixed point

				// Converged when the step is at machine precision, OR when steps are already small and have
				// stopped shrinking. The second case is the rounding-noise floor: near criticality I - J is
				// ill-conditioned (condition ~ 1/(1-rho)) and amplifies the rounding in F, so steps bottom
				// out above machine precision. (Newton steps shrink quadratically, and even the slow
				// near-critical phase halves them, so "shrinking by < 10%" only happens at the floor.)
				const double size  = step.lpNorm<Eigen::Infinity>();
				const double scale = std::max(1.0, x.lpNorm<Eigen::Infinity>());
				if(size <= 1e-15 * scale or (size <= 1e-8 * scale and size >= 0.9 * previous_size)) return {x, true};
				previous_size = size;
			}
			return {x, false};
		}

		/// m(a) = minimum cost of a finite tree rooted at a; +inf if a cannot derive any finite tree.
		/// Value iteration (Knuth's generalization of Dijkstra). With costs >= 0 a cheapest tree never needs
		/// to repeat a nonterminal along a path, so its height is <= K and K+1 sweeps reach the fixed point.
		template<typename CostF>
		std::vector<double> min_tree_costs(const LocalGrammar& L, const CostF& cost) {
			std::vector<double> m(L.size(), infinity);
			for(size_t sweep=0; sweep<=L.size()+1; sweep++) {
				bool changed = false;
				for(size_t a=0;a<L.size();a++) {
					for(const LocalRule& r : L.rules[a]) {
						double v = cost(*r.rule);
						for(size_t b : r.kids) v += m[b];
						if(v < m[a]) { m[a] = v; changed = true; }
					}
				}
				if(not changed) return m;
			}
			throw std::runtime_error("*** Internal error: minimum tree cost did not converge.");
		}

		/// Maximum cost of a finite tree from start (local 0); +inf if the grammar is recursive.
		/// Same iteration as above with max. Without recursion every tree has height <= K, so it settles
		/// within K+1 sweeps; if it is still growing after that, some nonterminal reachable from start can
		/// contain itself, and with costs > 0 tree costs from start are unbounded.
		template<typename CostF>
		double max_tree_cost(const LocalGrammar& L, const CostF& cost) {
			std::vector<double> m(L.size(), -infinity); // -inf = "no tree known yet"
			for(size_t sweep=0; sweep<=L.size()+1; sweep++) {
				bool changed = false;
				for(size_t a=0;a<L.size();a++) {
					for(const LocalRule& r : L.rules[a]) {
						double v = cost(*r.rule);
						for(size_t b : r.kids) v += m[b];
						if(v > m[a]) { m[a] = v; changed = true; }
					}
				}
				if(not changed) return m[0];
			}
			return infinity;
		}

	} // namespace detail


	// =========================================================================================
	//
	//  1. EXPECTED LENGTH
	//
	//  Expected tree cost x = (I - M)^{-1} c, finite iff rho(M) < 1 (see the file documentation).
	//
	// =========================================================================================

	struct Result {
		nonterminal_t       start = 0;
		bool                finite = false;
		double              expected_length = NaN;        ///< +inf if not finite
		double              spectral_radius = NaN;        ///< rho(M) over nonterminals reachable from start
		std::vector<bool>   reachable;                    ///< reachable[i]: nonterminal i is reachable from start
		std::vector<double> expected_length_by_nt;        ///< for reachable i (all +inf if !finite); NaN if unreachable
	};

	/**
	 * @brief Expected (cost-weighted) size of a tree generated from `start`.
	 * @param cost               functor const Rule& -> double, finite and >= 0 (default: 1 per node).
	 * @param critical_tolerance rho >= 1 - critical_tolerance is treated as infinite.
	 * @throws std::invalid_argument / std::runtime_error as described in the file documentation.
	 */
	template<typename G, RuleCost CostF = UnitCost>
	Result analyze(const G& grammar, const nonterminal_t start, CostF cost = CostF{}, const double critical_tolerance = 1e-10) {
		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))
			throw std::invalid_argument("*** critical_tolerance must be in [0,1)");

		const detail::LocalGrammar L = detail::make_local(grammar, start);
		const detail::Expectation  E = detail::expected_cost(L, cost, critical_tolerance);

		Result out;
		out.start           = start;
		out.finite          = E.finite;
		out.spectral_radius = E.rho;
		out.reachable.assign(G::N_NTs, false);
		out.expected_length_by_nt.assign(G::N_NTs, NaN);
		for(size_t a=0;a<L.size();a++) {
			out.reachable[L.nt[a]]             = true;
			out.expected_length_by_nt[L.nt[a]] = E.finite ? E.x(a) : infinity;
		}
		out.expected_length = out.expected_length_by_nt[start];
		return out;
	}

	/// From the grammar's start symbol (its output_t), as used by Grammar::generate() by default.
	template<typename G, RuleCost CostF = UnitCost>
	Result analyze(const G& grammar, CostF cost = CostF{}, const double critical_tolerance = 1e-10) {
		return analyze(grammar, G::template nt<typename G::output_t>(), cost, critical_tolerance);
	}

	/// Expected number of nodes (= E[Node::count()]) of a tree generated from `start`; +inf if not finite.
	template<typename G, RuleCost CostF = UnitCost>
	double expected_length(const G& grammar, const nonterminal_t start, CostF cost = CostF{}, const double critical_tolerance = 1e-10) {
		return analyze(grammar, start, cost, critical_tolerance).expected_length;
	}

	/// Expected number of nodes of a tree generated from the grammar's start symbol; +inf if not finite.
	template<typename G>
	double expected_length(const G& grammar) {
		return analyze(grammar).expected_length;
	}


	// =========================================================================================
	//
	//  2. MAXIMUM-ENTROPY PROBABILITIES WITH A GIVEN EXPECTED LENGTH
	//
	//  Goal. Among all distributions P over trees from `start`, maximize the entropy
	//  H(P) = -Σ_t P(t) log P(t) subject to E_P[cost(t)] = L.
	//
	//  Answer. The maximizer is the Gibbs distribution P(t) = exp(-λ·cost(t)) / Z, with λ chosen to
	//  meet the constraint. Because exp(-λ·cost(t)) is a product over the nodes of t, P is itself a
	//  PCFG. Give each rule the weight u_r = exp(-λ·w_r) and define the partition functions
	//
	//      Z_a = Σ_{r ∈ rules[a]} u_r · Π_{b ∈ kids(r)} Z_b        (least fixed point)
	//
	//  = the total weight of all trees rooted at a. Then the rule probabilities
	//
	//      p_r = u_r · Π_{b ∈ kids(r)} Z_b / Z_a
	//
	//  multiply along any tree to exp(-λ·cost(t)) / Z_start (each Z_b cancels against the child's
	//  denominator). So the best PCFG is also the best distribution overall, and it is unique.
	//
	//  Intuition: this is NOT uniform over rules. A rule whose children can be expanded in more ways
	//  (larger Z_b) gets more mass, which is what makes the distribution over whole programs flattest.
	//  λ > 0 penalizes length; for a non-recursive grammar a target above the "uniform" mean needs λ < 0.
	//  Entropy of the result: H = λ·L + log Z_start.
	//
	//  Method. E(λ) is strictly decreasing in λ (dE/dλ = -Var(cost) < 0), so we bisect on λ. Each
	//  evaluation solves for Z by Newton's method and computes E with expected_cost(). A λ that is too
	//  small makes the sums diverge (no finite Z, or rho >= 1); that simply counts as "E too large".
	//
	//  Correctness of each evaluation is certified, not assumed: the p_r must sum to 1 (Z is a fixed
	//  point), and rho < 1 for the induced PCFG. A fixed point with rho < 1 is necessarily the LEAST
	//  fixed point (the system is monotone and convex), so it is the right Z.
	//
	// =========================================================================================

	struct MaxEntResult {
		nonterminal_t       start = 0;
		double              target_length   = NaN;
		double              achieved_length = NaN;  ///< E[cost] under the new probabilities
		double              lambda          = NaN;  ///< the Lagrange multiplier λ
		double              min_length      = NaN;  ///< cost of the cheapest tree from start
		double              max_length      = NaN;  ///< cost of the most expensive tree (+inf if recursive)
		double              entropy         = NaN;  ///< entropy of the tree distribution, in nats
		double              log_partition   = NaN;  ///< log Z_start = log Σ_t exp(-λ·cost(t))
		double              spectral_radius = NaN;  ///< rho of the new mean matrix (< 1)
		std::vector<bool>   modified;               ///< modified[i]: the rules of nonterminal i were set
	};

	/**
	 * @brief Set the rule probabilities of `grammar` to the maximum-entropy PCFG over trees from `start`
	 *        whose expected cost is `target_length` (with UnitCost: E[Node::count()] == target_length).
	 *
	 * What is changed: only nonterminals reachable from `start`; only their p > 0 rules (p == 0 rules stay
	 * disabled); `p`, `logp`, `Grammar::Z`, `Grammar::logZ` are kept consistent, as change_probability does.
	 * Rules are NOT re-sorted, because existing Nodes point into the rule vectors.
	 * Strong exception guarantee: if anything throws, the grammar is unchanged.
	 *
	 * @param cost     strictly positive per-rule cost (default 1 per node).
	 * @param rel_tol  required |E - L| <= rel_tol·L; throws if unattainable (see the conditioning note above).
	 * @throws std::invalid_argument if L is outside (min_length, max_length) or the cost is not > 0;
	 *         std::runtime_error if a reachable nonterminal cannot derive a finite tree, or rel_tol cannot be met.
	 */
	template<typename G, RuleCost CostF = UnitCost>
	MaxEntResult set_max_entropy_probabilities(G& grammar, const double target_length, const nonterminal_t start,
	                                           CostF cost = CostF{}, const double rel_tol = 1e-9,
	                                           const double critical_tolerance = 1e-10) {
		using detail::LocalGrammar;
		if(not std::isfinite(target_length) or target_length <= 0.0)                              throw std::invalid_argument("*** target_length must be finite and > 0");
		if(not (rel_tol > 0.0 and rel_tol < 1.0))                         throw std::invalid_argument("*** rel_tol must be in (0,1)");
		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))  throw std::invalid_argument("*** critical_tolerance must be in [0,1)");

		const LocalGrammar base = detail::make_local(grammar, start);
		const size_t K = base.size();

		// --- Step 1: costs must be > 0, and every reachable nonterminal must derive some finite tree ----
		for(size_t a=0;a<K;a++) {
			for(const auto& r : base.rules[a]) {
				const double w = cost(*r.rule);
				if(not std::isfinite(w) or w <= 0.0)
					throw std::invalid_argument("*** set_max_entropy_probabilities requires finite costs > 0; got " + std::to_string(w) + " for " + r.rule->string());
			}
		}
		const std::vector<double> m = detail::min_tree_costs(base, cost);
		for(size_t a=0;a<K;a++)
			if(not std::isfinite(m[a]))
				throw std::runtime_error("*** Nonterminal " + std::to_string(base.nt[a]) + " is reachable from start but cannot generate any finite tree.");

		// --- Step 2: the target must lie strictly between the cheapest and the most expensive tree ----
		const double min_cost = m[0], max_cost = detail::max_tree_cost(base, cost);
		if(not (target_length > min_cost * (1.0 + 1e-12)))
			throw std::invalid_argument("*** target_length " + std::to_string(target_length) + " must exceed the minimum tree cost " + std::to_string(min_cost));
		if(not (target_length < max_cost * (1.0 - 1e-12)))
			throw std::invalid_argument("*** target_length " + std::to_string(target_length) + " must be below the maximum tree cost " + std::to_string(max_cost) + " (grammar is not recursive from start)");

		// --- Step 3: rescale to avoid underflow --------------------------------------------------------
		// Z_a is about exp(-λ·m_a), which underflows for large λ. Write Z_a = exp(-λ·m_a)·Ẑ_a. The system for
		// Ẑ has the same form with rule weights exp(-λ·excess_r), where excess_r = w_r + Σ m_kids - m_a >= 0
		// is how much more the cheapest tree using rule r costs than the cheapest tree overall. So Ẑ >= 1.
		std::vector<std::vector<double>> excess(K);
		for(size_t a=0;a<K;a++) {
			for(const auto& r : base.rules[a]) {
				double e = cost(*r.rule) - m[a];
				for(size_t b : r.kids) e += m[b];
				excess[a].push_back(std::max(0.0, e)); // e >= 0 exactly; clamp rounding
			}
		}

		// --- Step 4: for a given λ, the max-entropy PCFG and its expected length ---------------------
		struct Solution { LocalGrammar Q; double E, rho, Zhat_start; };
		auto solve = [&](const double lambda) -> std::optional<Solution> {
			std::vector<std::vector<double>> u(K); // rescaled rule weights
			for(size_t a=0;a<K;a++)
				for(double e : excess[a]) u[a].push_back(std::exp(-lambda * e));

			const detail::FixedPoint Zhat = detail::least_fixed_point(base, u, 1000);
			if(not Zhat.converged) return std::nullopt; // sums diverge: λ too small

			// p_r = u_r · Π Ẑ_kids / Ẑ_a. At a true fixed point these sum to 1 for every a.
			LocalGrammar Q = base;
			for(size_t a=0;a<K;a++) {
				double total = 0.0;
				for(size_t k=0;k<Q.rules[a].size();k++) {
					double p = u[a][k] / Zhat.x(a);
					for(size_t b : Q.rules[a][k].kids) p *= Zhat.x(b);
					Q.rules[a][k].p = p;
					total += p;
				}
				if(not (std::abs(total - 1.0) <= 1e-10)) return std::nullopt; // not a fixed point
				for(auto& r : Q.rules[a]) {
					r.p /= total;                                                 // remove rounding
					// For large λ a probability can underflow to exactly 0. Stored as 0, the rule would count
					// as disabled (p == 0) in every later call, so results would depend on call history.
					// Keep it at the smallest normal double instead; its effect on E is below 1e-300.
					r.p = std::max(r.p, std::numeric_limits<double>::min());
				}
			}

			const detail::Expectation E = detail::expected_cost(Q, cost, critical_tolerance);
			if(not E.finite) return std::nullopt; // rho >= 1: not the least fixed point, or critical
			return Solution{std::move(Q), E.x(0), E.rho, Zhat.x(0)};
		};

		// --- Step 5: find λ with E(λ) = target_length by bisection --------------------------------------------------
		// too_long(λ) is true for small λ and false for large λ, switching exactly where E(λ) = target_length.
		auto too_long = [&](const double lambda) { auto s = solve(lambda); return not s or s->E > target_length; };

		double hi = 1.0;                                    // grow hi until E(hi) < target_length
		for(int k=0; too_long(hi); k++) {
			if(k > 60) throw std::runtime_error("*** Could not find λ giving expected length below the target.");
			hi = 2.0*hi + 1.0;
		}
		double lo = hi - 1.0;                               // move lo down until E(lo) > target_length (or diverges)
		for(int k=0; not too_long(lo); k++) {
			if(k > 60) throw std::runtime_error("*** Could not find λ giving expected length above the target.");
			lo = hi - 2.0*(hi - lo);
		}
		for(int it=0; it<200; it++) {                       // bisect down to adjacent doubles
			const double mid = lo + 0.5*(hi - lo);
			if(mid <= lo or mid >= hi) break;
			(too_long(mid) ? lo : hi) = mid;
		}

		// Use whichever end of the final bracket is feasible and closer to target_length.
		double lambda = hi;
		std::optional<Solution> best = solve(hi);
		if(auto s = solve(lo); s and (not best or std::abs(s->E - target_length) < std::abs(best->E - target_length))) { best = std::move(s); lambda = lo; }
		if(not best or not (std::abs(best->E - target_length) <= rel_tol * target_length))
			throw std::runtime_error("*** Could not attain target length " + std::to_string(target_length) + " to rel_tol=" + std::to_string(rel_tol) +
			                         (best ? " (closest achieved " + std::to_string(best->E) + ")" : std::string("")) +
			                         "; E is extremely sensitive to λ here -- loosen rel_tol or lower the target.");

		// --- Step 6: report, then write the probabilities into the grammar (nothing below can throw) ---
		MaxEntResult out;
		out.start           = start;
		out.target_length   = target_length;
		out.achieved_length = best->E;
		out.lambda          = lambda;
		out.min_length      = min_cost;
		out.max_length      = max_cost;
		out.log_partition   = -lambda * min_cost + std::log(best->Zhat_start); // undo the rescaling of Step 3
		out.entropy         = lambda * best->E + out.log_partition;             // H = λ·E[cost] + log Z for a Gibbs distribution
		out.spectral_radius = best->rho;
		out.modified.assign(G::N_NTs, false);

		for(size_t a=0;a<K;a++) {
			const size_t i = best->Q.nt[a];
			double Z = 0.0;
			for(const auto& r : best->Q.rules[a]) {
				Rule& rule = grammar.rules[i][r.index];
				rule.p    = r.p;
				rule.logp = std::log(r.p);
				Z        += r.p;
			}
			grammar.Z[i]    = Z;  // p == 0 rules contribute nothing to Z and are left as they were
			grammar.logZ[i] = std::log(Z);
			out.modified[i] = true;
		}
		return out;
	}

	/// Same, from the grammar's start symbol (its output_t).
	template<typename G, RuleCost CostF = UnitCost>
	MaxEntResult set_max_entropy_probabilities(G& grammar, const double target_length, CostF cost = CostF{}, const double rel_tol = 1e-9) {
		return set_max_entropy_probabilities(grammar, target_length, G::template nt<typename G::output_t>(), cost, rel_tol);
	}


	// =========================================================================================
	//
	//  3. DOES THE GRAMMAR ASSIGN PROBABILITY TO INFINITE TREES?  (is the PCFG "tight"?)
	//
	//  Not the same question as infinite expected length: a critical grammar has infinite mean size
	//  but finite trees with probability 1.
	//
	//  Exact criterion. Let q_a = P(tree from a is finite). From start, P(infinite) > 0 iff
	//   (a) start can reach an UNPRODUCTIVE nonterminal (one that cannot derive any finite tree, so
	//       q = 0), or
	//   (b) start can reach a SUPERCRITICAL strongly connected component C of the nonterminal graph:
	//       rho(M_C) > 1, with M_C the mean matrix restricted to C.
	//  Critical components are harmless: a productive class dies out with probability 1, so it emits
	//  only finitely many children into later components (induct over the components).
	//  (a) is decided combinatorially. (b) compares rho with 1, so |rho - 1| <= critical_tolerance is
	//  treated as critical (tight) and reported as near_critical: check it when the distinction matters.
	//
	//  We also compute q numerically (least fixed point of q_a = Σ_r p_r Π q_kids). Near criticality
	//  the numbers are inherently imprecise: 1 - q is resolvable only to ~1e-8 for one critical
	//  component, and ~1e-4 for two chained critical components. So reported probabilities follow the
	//  exact decision: exactly 1 (tight) or 0 (unproductive) where it applies. Where they do not --
	//  a reachable supercritical component -- prob_infinite is the numerical value, which has the same
	//  ABSOLUTE accuracy limit: for rho = 1 + d the true P(infinite) is O(d), so for d below ~1e-7 the
	//  value is only an order-of-magnitude estimate (measured: true 2.0e-9 reported as 8.7e-9). The
	//  decision infinite_trees_possible itself is exact up to critical_tolerance.
	//
	// =========================================================================================

	struct TightnessResult {
		nonterminal_t       start = 0;
		bool                infinite_trees_possible = false; ///< THE ANSWER: P(infinite tree) > 0 (see near_critical)
		bool                unproductive_reachable  = false; ///< reason (a)
		bool                supercritical_reachable = false; ///< reason (b)
		bool                near_critical           = false; ///< some reachable recursive component has |rho - 1| <= tolerance
		double              max_component_rho       = 0.0;   ///< largest component rho (0 if nothing is recursive)
		double              prob_infinite           = NaN;   ///< 1 - q_start: exactly 0 if tight; numerical otherwise (absolute error up to ~1e-8 near rho = 1)
		double              prob_infinite_numerical = NaN;   ///< raw numerical 1 - q_start, for diagnostics
		std::vector<bool>   reachable;                       ///< reachable[i] from start
		std::vector<bool>   productive;                      ///< reachable i that can derive a finite tree
		std::vector<bool>   tight_by_nt;                     ///< reachable i whose own trees are finite with probability 1
		std::vector<double> prob_finite_by_nt;               ///< q_i (1 if tight, 0 if unproductive, numerical otherwise); NaN unreachable
		std::vector<double> component_rho_by_nt;             ///< rho of i's component (0 if not recursive); NaN unreachable
	};

	template<typename G>
	TightnessResult analyze_tightness(const G& grammar, const nonterminal_t start, const double critical_tolerance = 1e-10) {
		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))
			throw std::invalid_argument("*** critical_tolerance must be in [0,1)");

		const detail::LocalGrammar L = detail::make_local(grammar, start);
		const size_t K = L.size();
		const Eigen::MatrixXd M = detail::mean_matrix(L);

		// --- (a) productive = can derive a finite tree = has a finite minimum tree size ---------------
		const std::vector<double> min_size = detail::min_tree_costs(L, UnitCost{});
		std::vector<bool> productive(K);
		for(size_t a=0;a<K;a++) productive[a] = std::isfinite(min_size[a]);

		// --- reach[a][b]: b can occur below a (paths of length >= 1), by transitive closure ----------
		std::vector<std::vector<bool>> reach(K, std::vector<bool>(K, false));
		for(size_t a=0;a<K;a++) for(const auto& r : L.rules[a]) for(size_t b : r.kids) reach[a][b] = true;
		for(size_t k=0;k<K;k++) for(size_t a=0;a<K;a++) if(reach[a][k]) for(size_t b=0;b<K;b++) if(reach[k][b]) reach[a][b] = true;

		// --- (b) spectral radius of each strongly connected component ----------------------------------
		// a and b share a component iff each reaches the other. A component is recursive iff its members
		// reach themselves; non-recursive singletons have rho = 0.
		std::vector<double> comp_rho(K, -1.0); // -1 = not yet assigned
		for(size_t a=0;a<K;a++) {
			if(comp_rho[a] >= 0.0) continue;
			std::vector<size_t> members{a};
			for(size_t b=0;b<K;b++) if(b != a and reach[a][b] and reach[b][a]) members.push_back(b);

			double rho = 0.0;
			if(reach[a][a]) {
				Eigen::MatrixXd B(members.size(), members.size());
				for(size_t x=0;x<members.size();x++) for(size_t y=0;y<members.size();y++) B(x,y) = M(members[x], members[y]);
				rho = detail::spectral_radius(B);
			}
			for(size_t b : members) comp_rho[b] = rho;
		}

		// --- combine: a nonterminal is tight iff neither it nor anything below it is "bad" ----------
		std::vector<bool> bad(K), tight(K);
		for(size_t a=0;a<K;a++) bad[a] = not productive[a] or comp_rho[a] > 1.0 + critical_tolerance;
		for(size_t a=0;a<K;a++) {
			tight[a] = not bad[a];
			for(size_t b=0;b<K;b++) if(reach[a][b] and bad[b]) tight[a] = false;
		}

		TightnessResult out;
		out.start = start;
		for(size_t a=0;a<K;a++) {
			if(not productive[a])                        out.unproductive_reachable  = true;
			if(comp_rho[a] > 1.0 + critical_tolerance)   out.supercritical_reachable = true;
			if(reach[a][a] and std::abs(comp_rho[a] - 1.0) <= critical_tolerance) out.near_critical = true;
			out.max_component_rho = std::max(out.max_component_rho, comp_rho[a]);
		}
		out.infinite_trees_possible = not tight[0]; // everything is reachable from start, so this is (a) or (b)

		// --- numerical termination probabilities q (diagnostic; see the section comment) -----------
		std::vector<std::vector<double>> p(K);
		for(size_t a=0;a<K;a++) for(const auto& r : L.rules[a]) p[a].push_back(r.p);
		const Eigen::VectorXd q = detail::least_fixed_point(L, p, 20000).x.cwiseMax(0.0).cwiseMin(1.0);

		// Where the problem is well conditioned, the numbers must confirm the decision. Near a critical
		// component they cannot: rounding limits 1 - q to ~sqrt(eps) ~ 1e-8 there whatever the method
		// (a valid grammar with rho = 1 - 1e-8 would fail a 1e-9 check). So check only when every
		// recursive component is at least 1e-4 away from rho = 1, where the error is ~1e-13.
		double gap = infinity; // distance of the closest recursive component from criticality
		for(size_t a=0;a<K;a++) if(reach[a][a]) gap = std::min(gap, std::abs(comp_rho[a] - 1.0));
		if(gap >= 1e-4)
			for(size_t a=0;a<K;a++)
				if(tight[a] and not (1.0 - q(a) <= 1e-9))
					throw std::runtime_error("*** analyze_tightness internal inconsistency: nonterminal " + std::to_string(L.nt[a]) +
					                         " decided tight but numerical P(infinite) = " + std::to_string(1.0 - q(a)));

		out.reachable.assign(G::N_NTs, false);
		out.productive.assign(G::N_NTs, false);
		out.tight_by_nt.assign(G::N_NTs, false);
		out.prob_finite_by_nt.assign(G::N_NTs, NaN);
		out.component_rho_by_nt.assign(G::N_NTs, NaN);
		for(size_t a=0;a<K;a++) {
			const size_t i = L.nt[a];
			out.reachable[i]           = true;
			out.productive[i]          = productive[a];
			out.tight_by_nt[i]         = tight[a];
			out.component_rho_by_nt[i] = comp_rho[a];
			out.prob_finite_by_nt[i]   = not productive[a] ? 0.0 : (tight[a] ? 1.0 : q(a));
		}
		out.prob_infinite           = 1.0 - out.prob_finite_by_nt[start];
		out.prob_infinite_numerical = 1.0 - (productive[0] ? q(0) : 0.0);
		return out;
	}

	template<typename G>
	TightnessResult analyze_tightness(const G& grammar) {
		return analyze_tightness(grammar, G::template nt<typename G::output_t>());
	}

	/// True iff trees generated from `start` are infinite with positive probability (the PCFG is not tight).
	/// Exactly critical components count as tight; see TightnessResult::near_critical.
	template<typename G>
	bool assigns_probability_to_infinite_trees(const G& grammar, const nonterminal_t start, const double critical_tolerance = 1e-10) {
		return analyze_tightness(grammar, start, critical_tolerance).infinite_trees_possible;
	}

	template<typename G>
	bool assigns_probability_to_infinite_trees(const G& grammar) {
		return analyze_tightness(grammar).infinite_trees_possible;
	}

} // namespace GrammarExpectedLength
