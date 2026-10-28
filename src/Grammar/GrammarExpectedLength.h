#pragma once

/**
 * @file GrammarExpectedLength.h
 * @brief Exact expected size of trees (programs) sampled from any Fleet Grammar, and the
 *        maximum-entropy rule probabilities that achieve a given expected size.
 *
 *   analyze(g[, start][, cost])                        -> Result (expected length, spectral radius, ...)
 *   expected_length(g[, start][, cost])                -> double (+inf if not finite)
 *   set_max_entropy_probabilities(g, L[, start][, cost]) -> MaxEntResult; modifies g in place
 *   assigns_probability_to_infinite_trees(g[, start])  -> bool  (is the PCFG non-tight?)
 *   analyze_tightness(g[, start])                      -> TightnessResult (why, rho per component, P(infinite))
 *
 * Tests: Testing/GrammarExpectedLength, Testing/GrammarMaxEntropy, Testing/GrammarTightness.
 *
 * =============================================================================================
 *  DOCUMENTATION (Markdown; rendered by Doxygen)
 * =============================================================================================
 *
 * Three exact (non-sampling) tools for any Fleet `Grammar`:
 *
 * | Function | What it does |
 * |---|---|
 * | `analyze(grammar[, start][, cost])` / `expected_length(...)` | Expected size of a tree from the grammar's current probabilities, plus the spectral radius that decides whether it is finite. |
 * | `set_max_entropy_probabilities(grammar, L[, start][, cost])` | Overwrites rule probabilities with the **maximum-entropy distribution over programs** whose expected size is `L`. |
 * | `assigns_probability_to_infinite_trees(grammar[, start])` / `analyze_tightness(...)` | Decides whether the grammar puts positive probability on infinite trees (is the PCFG non-"tight"?), says why, and gives P(infinite). |
 *
 * All live in `namespace GrammarExpectedLength`, are templates over the grammar type (so they work with any `Grammar<input_t, output_t, TYPES...>` or subclass), and need Eigen (`#include "EigenLib.h"` is pulled in by the header).
 *
 * ```cpp
 * #include "EigenLib.h"
 * #include "Grammar.h"
 * #include "GrammarExpectedLength.h"
 * using namespace GrammarExpectedLength;
 *
 * double E = expected_length(grammar);                 // E[Node::count()] from nt<output_t>(); +inf if not finite
 * Result R = analyze(grammar);                         // full report: rho, per-nonterminal values, M, ...
 * double leaves = analyze(grammar, TerminalCost{}).expected_length;   // E[Node::count_terminals()]
 *
 * MaxEntResult m = set_max_entropy_probabilities(grammar, 8.0);       // now E[Node::count()] == 8
 *
 * bool bad = assigns_probability_to_infinite_trees(grammar);          // P(infinite tree) > 0 ?
 * TightnessResult T = analyze_tightness(grammar);                      // reasons, rho per component, P(infinite)
 * ```
 *
 * "Length" means number of nodes (= rule expansions), i.e. `Node::count()`. Any nonnegative per-rule cost can be used instead via a functor `double(const Rule&)` (`UnitCost`, `TerminalCost`, `InternalCost` are provided; e.g. `[](const Rule& r){ return 1.0 + r.N; }`).
 *
 * ---
 *
 * ## 1. Expected length: `analyze`
 *
 * ### Math
 * Sampling from a Fleet grammar is a **multitype Galton–Watson branching process** whose types are the nonterminals. Let
 *
 *   M(i,j) = E[# children of type j | expanding i] = Σ_{r∈R_i} (p_r / Z_i) · #{k : child_types_k = j},
 *   c(i) = Σ_{r∈R_i} (p_r / Z_i) · w(r).
 *
 * By linearity over generations, the expected total cost of a tree rooted at i is
 *
 *   x = c + Mc + M²c + … = (I − M)⁻¹ c,
 *
 * and this Neumann series converges iff the **spectral radius ρ(M) < 1**. For nonnegative M, ρ is the Perron eigenvalue: the asymptotic per-generation growth factor of the expected population.
 *
 * | ρ | process | E[size] |
 * |---|---|---|
 * | < 1 | subcritical | finite — returned |
 * | = 1 | critical (trees finite a.s.) | ∞ |
 * | > 1 | supercritical (infinite trees with positive prob.) | ∞ |
 *
 * Example: `S → f(S,S) [q] | x [1−q]` has ρ = 2q and E = 1/(1−2q). At q = ½, every tree is finite yet the mean size is infinite.
 *
 * ### Implementation details that matter for correctness
 * * Only nonterminals **reachable from `start` through p > 0 rules** enter ρ. An unreachable supercritical or empty part of the grammar does not affect the answer.
 * * Z_i is recomputed from `rules[i]` rather than read from the cached `Grammar::Z`.
 * * A reachable nonterminal with no positive-probability rule throws (`generate()` would fail there).
 * * Solve uses full-pivot LU + one step of iterative refinement, then verifies: residual ≤ 1e-9 and x ≥ c ≥ 0 (true for the exact solution because (I−M)⁻¹ = Σ Mᵏ ≥ 0). Any failure throws rather than returning a doubtful number.
 * * ρ ≥ 1 − `critical_tolerance` (default 1e-10) is reported as infinite: doubles cannot distinguish ρ = 1 from ρ = 1 − 1e-16, and the mean there would be ~1/(1−ρ) anyway.
 *
 * ### Result fields
 * `finite`, `expected_length` (+inf if not finite), `spectral_radius`, `reachable[i]`, `expected_length_by_nt[i]` (for every reachable i; NaN if unreachable), `mean_matrix` (rows = parent), `expected_cost`, `relative_residual`.
 *
 * ### Caveats
 * * `Grammar::generate()` rejects trees deeper than `GRAMMAR_MAX_DEPTH` (default 64) and retries, so it samples the PCFG **conditioned** on depth. Empirical means will be smaller than this exact value whenever depth truncation has non-negligible probability (i.e. close to criticality).
 * * A subclass that overrides `sample_rule` with a different distribution is not described by this computation.
 *
 * ---
 *
 * ## 2. Max-entropy probabilities: `set_max_entropy_probabilities`
 *
 * ### What is maximized
 * The entropy of the **distribution over whole programs** (derivation trees) from `start`,
 * H(P) = −Σ_t P(t) log P(t), subject to E_P[cost(t)] = L.
 *
 * ### Solution
 * The Lagrangian gives the Gibbs distribution P*(t) = e^{−λ·cost(t)} / Z_start(λ). Because e^{−λ cost} factorizes over nodes, P* **is itself a PCFG**: with rule weights u_r = e^{−λ w_r}, partition functions
 *
 *   Z_i = Σ_{r∈R_i} u_r Π_{c∈children(r)} Z_c  (least fixed point),
 *
 * the rule probabilities p_r = u_r Π Z_c / Z_i telescope to P*(t). Hence max-entropy over programs and max-entropy over PCFG parameters coincide and the answer is unique.
 *
 * Note this is **not** "uniform over rules within each nonterminal". A rule whose children can be expanded in more ways (larger Z_c) gets more mass. That is what makes the distribution over programs as flat as possible within the length budget. Symmetric rules (e.g. `and`/`or`) do get identical probabilities.
 *
 * Useful identities (the function checks the first one internally as a certificate):
 * * H(P*) = λ·L + log Z_start, and also H = Σ_i v_i H_i with v = expected visits to each nonterminal.
 * * For any other PCFG Q on the same rules with E_Q[cost] = L: H(P*) − H(Q) = KL(Q‖P*) ≥ 0.
 * * λ > 0 (penalizing length) is the usual case. For a non-recursive grammar a target above the "uniform tree" mean gives λ < 0, which is valid.
 *
 * ### Algorithm
 * 1. Validate; collect available (p > 0) rules; reachability from `start`.
 * 2. Minimum tree cost m_i (Knuth's generalization of Dijkstra). Partition functions are rescaled Z_i = e^{−λ m_i}·Ẑ_i, so Ẑ ≥ 1 and nothing underflows for large λ.
 * 3. For fixed λ: Newton's method from 0 for the least fixed point of the monotone polynomial system (Esparza, Kiefer & Luttenberger 2010). Correctness does **not** depend on that convergence theorem. A fixed point x with ρ(F′(x)) < 1 must be the least one by convexity, and the induced PCFG's mean matrix is diag(Ẑ)⁻¹F′(Ẑ)diag(Ẑ), with the same ρ. The same `solve_block` used by `analyze` verifies ρ < 1 and gives E(λ).
 * 4. E(λ) is strictly decreasing (dE/dλ = −Var(cost) < 0), so λ is bracketed and bisected to double precision ("no subcritical solution" counts as "E too large").
 * 5. Entropy is computed two ways (visits × local entropies vs. λL + log Z) and must agree to 1e-7, which certifies the Gibbs form.
 * 6. Only after every check passes are `p`, `logp`, `Grammar::Z`, `Grammar::logZ` written (**strong exception guarantee**).
 *
 * ### Semantics you need to know
 * * Rules with `p == 0` are treated as **disabled** and stay 0. All rules with p > 0 are available; their current values are otherwise ignored.
 * * Only nonterminals reachable from `start` are modified (`MaxEntResult::modified`). Others cannot affect trees from `start` and are left untouched.
 * * Costs must be **strictly positive** on the available reachable rules, so `TerminalCost` is rejected.
 * * Required: min_cost < L < max_cost (max_cost = ∞ if anything reachable is recursive). Otherwise `std::invalid_argument`.
 * * Rules are **not re-sorted**. Existing `Node`s hold `Rule*` pointers into the rule vectors, so re-sorting would silently corrupt them. `change_probability` behaves the same way. Within-nonterminal order may therefore differ from a freshly built grammar; terminals still come first.
 *
 * ### Conditioning limit for large targets
 * Near criticality E ≈ 1/(1−ρ), so
 *
 *   relative error of E ≈ L² × (relative error of the stored double probabilities ≈ 1e-16).
 *
 * No algorithm can beat this, because the result has to be stored in doubles. Measured on the RationalRules grammar:
 *
 * | L | rel_tol 1e-9 | rel_tol 1e-6 | rel_tol 1e-3 |
 * |---|---|---|---|
 * | 10², 10³ | ok | ok | ok |
 * | 10⁴ | refused | ok | ok |
 * | 10⁵ | refused | ok | ok |
 * | 10⁶ | refused | refused | ok (3.5e-5) |
 *
 * The function throws (`std::runtime_error`, grammar unchanged) instead of returning a result outside `rel_tol`. Pass a looser `rel_tol` if you need very long expected programs.
 *
 * ### MaxEntResult fields
 * `target_length`, `achieved_length`, `lambda`, `min_length`, `max_length`, `entropy` (nats), `log_partition`, `spectral_radius`, `modified[i]`, `expected_visits[i]`.
 *
 * ---
 *
 * ## 3. Infinite trees: `assigns_probability_to_infinite_trees` / `analyze_tightness`
 *
 * ### Why this is a separate question
 * Finite expected length implies finite trees, but not conversely. `S → f(S,S) [½] | x [½]` is **critical**: every tree is finite with probability 1, yet E[size] = ∞. So "does the grammar put mass on infinite trees?" (tightness/consistency) needs its own test.
 *
 * | grammar from start | E[size] (`analyze`) | infinite trees? |
 * |---|---|---|
 * | all reachable components ρ < 1 | finite | no |
 * | some critical component (ρ = 1), none supercritical, all productive | ∞ | **no** |
 * | some supercritical component (ρ > 1) | ∞ | yes |
 * | reaches an unproductive nonterminal | ∞ | yes |
 *
 * ### Exact criterion
 * q_i = P(tree from i is finite) is the least fixed point of q_i = Σ_r p_r Π_c q_c. Then:
 *
 * 1. **Unproductive** nonterminals (no finite derivation through p > 0 rules) have q = 0. If start can reach one, P(infinite) > 0. This is decided purely combinatorially, with no floating point.
 * 2. Otherwise every reachable nonterminal is productive. P(infinite) > 0 iff some reachable **strongly connected component** C of the dependency graph has ρ(M_C) > 1.
 *    * Critical components are harmless. A productive class cannot be "singular" (one lineage forever), so it dies out a.s. It therefore emits finitely many individuals into downstream classes; induct over the DAG of components.
 *    * Components are found with Tarjan's algorithm; each ρ is checked against Perron–Frobenius row-sum bounds.
 *
 * ### Floating point: the one judgment call
 * Probabilities are doubles, so ρ = 1 cannot be distinguished from 1 ± 1e-16. Components with |ρ − 1| ≤ `critical_tolerance` (default 1e-10) set `near_critical = true` and are treated as critical, i.e. tight. If the true ρ were 1 + δ, the true P(infinite) would be O(δ), at floating-noise level. **Check `near_critical` whenever the distinction matters to you.**
 *
 * Numerically computing q cannot replace the exact decision. Near criticality F(q) − q ~ (1−q)², so 1 − q is resolvable only to ~√ε ≈ 1e-8 for one critical class. For a chain of k critical classes feeding each other it is only ~ε^(1/2^k): measured 8.6e-5 for k = 2. Therefore:
 * * `prob_infinite` (and `prob_finite_by_nt`) are **exactly** 0 (resp. 1) wherever the exact criterion says tight, exactly 1 (resp. 0) for unproductive nonterminals, and the numerical least fixed point (Newton from 0, monotone) otherwise.
 * * `prob_infinite_numerical` keeps the raw Newton estimate for diagnostics.
 * * Where the problem is well conditioned (no near-critical component), the code asserts that the numerics agree with the decision to 1e-9.
 *
 * ### TightnessResult fields
 * `infinite_trees_possible` (the answer), `unproductive_reachable`, `supercritical_reachable`, `near_critical`, `max_component_rho`, `prob_infinite`, `prob_infinite_numerical`, `reachable[i]`, `productive[i]`, `tight_by_nt[i]`, `prob_finite_by_nt[i]`, `component_rho_by_nt[i]`.
 *
 * Same conventions as `analyze`: p = 0 rules are ignored, unreachable nonterminals are ignored (NaN in per-nonterminal fields), and a reachable nonterminal with no positive-probability rule throws.
 *
 * ---
 *
 * ## Tests
 * `Testing/GrammarExpectedLength` (1497 checks), `Testing/GrammarMaxEntropy` (1004 checks), `Testing/GrammarTightness` (7408 checks); `make test` in each. All pass under `make sanitize` (ASan + UBSan). ASan's only reports are leaks from Fleet's own `Grammar::add` (`new FT(...)`, Grammar.h:369), which the tests trigger by building thousands of grammars.
 *
 * * **Expected length**: closed forms (incl. critical/supercritical, change_probability), a RationalRules-shaped grammar (E = 3 by hand), 400 random 4-type grammars against an independent fixed-point iteration from the rules, and Monte Carlo through `Grammar::generate()` with `Node::count()` / `count_terminals()`.
 * * **Tightness**: closed forms for P(infinite) (incl. critical = tight with infinite mean), unproductive cases (direct, indirect with P(inf) = 0.01, disabled escape, zero-probability path), component structures (critical→subcritical, critical→critical, subcritical→supercritical with P(inf) = 0.2·⅔, unreachable supercritical, mutual recursion with ρ = √(2p)), Monte Carlo via `Grammar::__generate`, and 1000 random grammars (752 tight / 65 supercritical / 36 unproductive) against an independent plain fixed-point iteration, plus consistency with `analyze`.
 * * **Max entropy**: closed forms (incl. λ and a finite language with λ = −ln 9), brute-force 1-D entropy maximization, the **Gibbs identity** `log_probability(t) + λ·cost(t) = −log Z` on sampled trees (exercises Fleet's cached `logp`/`logZ`), Monte Carlo of length and entropy, **optimality vs 198 random PCFGs** (H(Q) ≤ H*), non-unit costs, symmetry, idempotence, no re-sorting, error cases + strong exception guarantee, and the conditioning table above.
 *
 * Note: built and tested here with g++ 13 using `-std=c++23` (g++ 13 lacks `-std=c++26`) and Eigen 3.3.90. Nothing in the header requires more than C++20 (concepts).
 *
 * =============================================================================================
 *  IMPLEMENTATION NOTES (expected length)
 * =============================================================================================
 *
 *
 * MATH (expected length; the max-entropy math is documented above set_max_entropy_probabilities)
 * ----
 * Sampling from a Fleet grammar (Grammar::generate) is a multitype Galton-Watson branching
 * process whose types are the nonterminals: a node of type i picks rule r with probability
 * p_r / Z_i (Z_i = sum of p over rules of type i) and then spawns one child of each type in
 * r.child_types. Define the mean (offspring) matrix
 *
 *        M(i,j) = E[ number of type-j children | expanding a type-i node ]
 *               = sum_{r in rules[i]} (p_r / Z_i) * #{ k : r.child_types[k] == j }
 *
 * and a per-rule cost w(r) >= 0 (w = 1 counts nodes, matching Node::count()). With
 * c(i) = E[w(r) | type i] = sum_r (p_r/Z_i) w(r), linearity of expectation over generations gives
 *
 *        x(i) = E[ total cost of a tree rooted at i ] = c(i) + sum_j M(i,j) x(j)
 *        x    = c + M c + M^2 c + ...  = (I - M)^{-1} c
 *
 * The Neumann series converges iff the spectral radius rho(M) < 1 (computed on the nonterminals
 * reachable from the start symbol -- unreachable parts of the grammar are irrelevant). For a
 * nonnegative M, rho(M) is the Perron eigenvalue: the asymptotic per-generation growth factor
 * of the expected number of nodes.
 *     rho < 1 : subcritical; trees are finite a.s. and E[size] is finite (we solve the system).
 *     rho = 1 : critical;    trees are finite a.s. but E[size] = infinity.
 *     rho > 1 : supercritical; trees are infinite with positive probability.
 * In the last two cases we return +infinity.
 *
 * CAVEATS
 * -------
 *  - This is the expectation of the *idealized* PCFG. Grammar::generate() throws DepthException
 *    past GRAMMAR_MAX_DEPTH and retries, i.e. it samples from the PCFG *conditioned* on depth <
 *    GRAMMAR_MAX_DEPTH. Empirical means of generate() are therefore (slightly) smaller whenever
 *    depth-truncation has non-negligible probability.
 *  - Assumes the standard Grammar sampling rule (prob p_r / Z_i). A subclass that overrides
 *    sample_rule with a different distribution is not described by this computation.
 *  - Z_i is recomputed from the rules here (not read from the cached Grammar::Z) so the result
 *    does not depend on the incremental cache maintained by add()/change_probability().
 */

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <type_traits>
#include <cassert>
#include <algorithm>

#include "EigenLib.h"   // must precede Grammar.h if Grammar.h has not been included yet (AM_I_USING_EIGEN)
#include "Grammar.h"

namespace GrammarExpectedLength {

	/// A cost functor maps a const Rule& to a (convertible-to-)double. The constraint keeps an
	/// integer start symbol, e.g. analyze(g, 0), from being deduced as a cost functor.
	template<typename F>
	concept RuleCost = std::is_invocable_r_v<double, const F&, const Rule&>;

	/// Cost = 1 per node: expected value equals E[Node::count()].
	struct UnitCost     { double operator()(const Rule& r) const { return 1.0; } };

	/// Cost = 1 per terminal (leaf) node: expected value equals E[Node::count_terminals()].
	struct TerminalCost { double operator()(const Rule& r) const { return r.is_terminal() ? 1.0 : 0.0; } };

	/// Cost = 1 per non-leaf node.
	struct InternalCost { double operator()(const Rule& r) const { return r.is_terminal() ? 0.0 : 1.0; } };

	namespace detail {

		struct BlockSolution {
			bool            finite = false;
			double          rho = 0.0;
			double          relative_residual = 0.0;
			Eigen::VectorXd x;  // valid only if finite
		};

		/**
		 * @brief Core numerics shared by analyze() and set_max_entropy_probabilities().
		 *        Given a nonnegative mean matrix M (K x K, every state reachable from the root) and a
		 *        nonnegative cost vector c, compute rho(M) and, if rho < 1 - critical_tolerance,
		 *        x = (I - M)^{-1} c with verification.
		 *
		 * For rho(M) < 1, (I-M) is a nonsingular M-matrix, so (I-M)^{-1} = sum_k M^k >= 0 and the
		 * exact solution satisfies x = c + M x >= c >= 0. We verify that, and the residual.
		 */
		inline BlockSolution solve_block(const Eigen::MatrixXd& Mr, const Eigen::VectorXd& cr, const double critical_tolerance) {
			const Eigen::Index K = Mr.rows();
			assert(Mr.cols() == K and cr.size() == K);
			BlockSolution out;

			// Spectral radius (Perron root of a nonnegative matrix)
			double rho = 0.0;
			if(K > 0) {
				Eigen::EigenSolver<Eigen::MatrixXd> es(Mr, /*computeEigenvectors=*/false);
				if(es.info() != Eigen::Success)
					throw std::runtime_error("*** Eigenvalue computation failed for the grammar mean matrix.");
				for(Eigen::Index k=0;k<K;k++) rho = std::max(rho, std::abs(es.eigenvalues()(k)));
			}
			// Sanity bound for a nonnegative matrix: rho <= max row sum (Gershgorin / Perron-Frobenius).
			const double max_row_sum = (K > 0 ? Mr.rowwise().sum().maxCoeff() : 0.0);
			if(not std::isfinite(rho) or rho > max_row_sum * (1.0 + 1e-9) + 1e-12)
				throw std::runtime_error("*** Spectral radius " + std::to_string(rho) + " inconsistent with max row sum " + std::to_string(max_row_sum));
			out.rho = rho;

			if(rho >= 1.0 - critical_tolerance) {
				out.finite = false;
				return out;
			}

			const Eigen::MatrixXd A = Eigen::MatrixXd::Identity(K, K) - Mr;
			Eigen::FullPivLU<Eigen::MatrixXd> lu(A);
			if(not lu.isInvertible())
				throw std::runtime_error("*** (I - M) is numerically singular despite rho = " + std::to_string(rho) + " < 1.");

			Eigen::VectorXd x = lu.solve(cr);
			x += lu.solve(cr - A * x); // one step of iterative refinement

			const double cnorm = cr.lpNorm<Eigen::Infinity>();
			const double rnorm = (cr - A * x).lpNorm<Eigen::Infinity>();
			out.relative_residual = (cnorm > 0.0 ? rnorm / cnorm : rnorm);

			const double xnorm = x.lpNorm<Eigen::Infinity>();
			for(Eigen::Index a=0;a<K;a++) {
				if(not std::isfinite(x(a)))
					throw std::runtime_error("*** Non-finite solution in expected length computation.");
				if(x(a) < cr(a) - 1e-9 * std::max(1.0, xnorm)) // x >= c up to rounding
					throw std::runtime_error("*** Expected length solution violates x >= c; numerically unreliable (rho = " + std::to_string(rho) + ").");
				x(a) = std::max(x(a), cr(a)); // clamp rounding below the exact bound
			}
			if(not (out.relative_residual <= 1e-9))
				throw std::runtime_error("*** Expected length linear solve has large residual " + std::to_string(out.relative_residual));

			out.finite = true;
			out.x = std::move(x);
			return out;
		}
	} // namespace detail

	struct Result {
		nonterminal_t       start = 0;
		bool                finite = false;
		double              expected_length = std::numeric_limits<double>::quiet_NaN(); ///< +inf if not finite
		double              spectral_radius = std::numeric_limits<double>::quiet_NaN(); ///< rho(M) on the reachable nonterminals

		std::vector<bool>   reachable;              ///< reachable[i]: nonterminal i is reachable from start via p>0 rules
		std::vector<double> expected_length_by_nt;  ///< x(i) for reachable i when finite; NaN for unreachable i;
		                                            ///< +inf for every reachable i when !finite
		Eigen::MatrixXd     mean_matrix;            ///< full N_NTs x N_NTs matrix M (rows = parent). Row i is 0 if Z_i == 0.
		Eigen::VectorXd     expected_cost;          ///< c(i) = expected cost of one expansion of i (0 if Z_i == 0)
		double              relative_residual = 0.0;///< ||(I-M)x - c||_inf / ||c||_inf on the reachable block (0 if !finite)
	};

	/**
	 * @brief Full analysis of the expected (cost-weighted) size of trees generated from `start`.
	 *
	 * @param grammar            any Fleet Grammar (or subclass) -- only rules[] and N_NTs are used.
	 * @param start              root nonterminal (e.g. G::template nt<typename G::output_t>()).
	 * @param cost               functor const Rule& -> double, must return finite values >= 0.
	 * @param critical_tolerance rho >= 1 - critical_tolerance is treated as critical (infinite).
	 *                           Near rho = 1 the expectation is ~ 1/(1-rho), i.e. ill-conditioned;
	 *                           floating point cannot distinguish rho = 1 from rho = 1 - 1e-16.
	 *
	 * @throws std::invalid_argument for malformed input: start out of range, a rule probability
	 *         that is NaN/inf/negative, a child type out of range, or a NaN/inf/negative cost.
	 * @throws std::runtime_error if a nonterminal reachable from start has no positive-probability
	 *         rule (generate() would fail there), or if the linear solve fails its numerical checks.
	 */
	template<typename G, RuleCost CostF = UnitCost>
	Result analyze(const G& grammar, const nonterminal_t start, CostF cost = CostF{}, const double critical_tolerance = 1e-10) {

		constexpr size_t N = G::N_NTs;
		static_assert(N > 0, "*** Grammar has no nonterminal types");

		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))
			throw std::invalid_argument("*** critical_tolerance must be in [0,1)");
		if(static_cast<size_t>(start) >= N)
			throw std::invalid_argument("*** start nonterminal " + std::to_string(start) + " out of range (N_NTs=" + std::to_string(N) + ")");

		const double inf = std::numeric_limits<double>::infinity();
		const double nan = std::numeric_limits<double>::quiet_NaN();

		Result out;
		out.start = start;
		out.mean_matrix   = Eigen::MatrixXd::Zero(N, N);
		out.expected_cost = Eigen::VectorXd::Zero(N);
		out.reachable.assign(N, false);
		out.expected_length_by_nt.assign(N, nan);

		// ---------------------------------------------------------------------------------
		// 1. Validate rules; build Z, M, c and the p>0 adjacency relation.
		//    Accumulate unnormalized sums and divide by Z once, to avoid N divisions per row.
		// ---------------------------------------------------------------------------------
		std::vector<double> Z(N, 0.0);
		std::vector<std::vector<bool>> edge(N, std::vector<bool>(N, false)); // edge[i][j]: some p>0 rule of i has a j child

		for(size_t i=0;i<N;i++) {
			for(const Rule& r : grammar.rules[i]) {

				if(static_cast<size_t>(r.nt) != i)
					throw std::invalid_argument("*** Rule stored under nonterminal " + std::to_string(i) + " has nt=" + std::to_string(r.nt) + ": " + r.string());
				if(not std::isfinite(r.p) or r.p < 0.0)
					throw std::invalid_argument("*** Rule probability must be finite and >= 0: " + r.string());

				const double w = static_cast<double>(cost(r));
				if(not std::isfinite(w) or w < 0.0)
					throw std::invalid_argument("*** Cost must be finite and >= 0 for rule " + r.string());

				for(const nonterminal_t t : r.get_child_types()) {
					if(static_cast<size_t>(t) >= N)
						throw std::invalid_argument("*** Child type out of range in rule " + r.string());
				}

				if(r.p == 0.0) continue; // never sampled: contributes nothing (and creates no reachability)

				Z[i] += r.p;
				out.expected_cost(i) += r.p * w;
				for(const nonterminal_t t : r.get_child_types()) {
					out.mean_matrix(i, t) += r.p;
					edge[i][t] = true;
				}
			}

			if(Z[i] > 0.0) {
				if(not std::isfinite(Z[i]))
					throw std::invalid_argument("*** Sum of rule probabilities overflowed for nonterminal " + std::to_string(i));
				out.mean_matrix.row(i) /= Z[i];
				out.expected_cost(i)   /= Z[i];
			}
		}

		// ---------------------------------------------------------------------------------
		// 2. Reachability from start (BFS over p>0 rules). Every reachable nonterminal must be
		//    expandable, otherwise generate() itself would fail.
		// ---------------------------------------------------------------------------------
		std::deque<size_t> q{static_cast<size_t>(start)};
		out.reachable[start] = true;
		while(not q.empty()) {
			const size_t i = q.front(); q.pop_front();
			for(size_t j=0;j<N;j++) {
				if(edge[i][j] and not out.reachable[j]) {
					out.reachable[j] = true;
					q.push_back(j);
				}
			}
		}

		std::vector<size_t> idx; // reachable nonterminals, in increasing order; idx[k] = original index
		for(size_t i=0;i<N;i++) {
			if(not out.reachable[i]) continue;
			if(Z[i] <= 0.0)
				throw std::runtime_error("*** Nonterminal " + std::to_string(i) + " is reachable from start " + std::to_string(start) +
				                         " but has no rule with positive probability; trees from start cannot be generated.");
			idx.push_back(i);
		}
		const Eigen::Index K = static_cast<Eigen::Index>(idx.size());

		Eigen::MatrixXd Mr(K, K);
		Eigen::VectorXd cr(K);
		for(Eigen::Index a=0;a<K;a++) {
			cr(a) = out.expected_cost(idx[a]);
			for(Eigen::Index b=0;b<K;b++)
				Mr(a,b) = out.mean_matrix(idx[a], idx[b]);
		}

		// Rows of reachable nonterminals cannot point outside the reachable set (by construction).
		// Check it anyway: if this ever failed, restricting M would silently drop mass.
		for(Eigen::Index a=0;a<K;a++) {
			const double full = out.mean_matrix.row(idx[a]).sum();
			const double rest = Mr.row(a).sum();
			if(std::abs(full - rest) > 1e-12 * std::max(1.0, full))
				throw std::runtime_error("*** Internal error: reachable block of the mean matrix is not closed.");
		}

		// ---------------------------------------------------------------------------------
		// 3./4. Spectral radius of the reachable block, then solve (I - M) x = c if rho < 1.
		// ---------------------------------------------------------------------------------
		const detail::BlockSolution sol = detail::solve_block(Mr, cr, critical_tolerance);
		out.spectral_radius   = sol.rho;
		out.relative_residual = sol.relative_residual;

		if(not sol.finite) {
			out.finite = false;
			out.expected_length = inf;
			for(size_t i : idx) out.expected_length_by_nt[i] = inf;
			return out;
		}
		const Eigen::VectorXd& x = sol.x;

		out.finite = true;
		for(Eigen::Index a=0;a<K;a++) out.expected_length_by_nt[idx[a]] = x(a);
		out.expected_length = out.expected_length_by_nt[start];
		return out;
	}

	/// Analysis from the grammar's start symbol (its output_t), as used by Grammar::generate() by default.
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
	//  MAXIMUM-ENTROPY RULE PROBABILITIES WITH A FIXED EXPECTED LENGTH
	//
	// =========================================================================================
	//
	// PROBLEM. Among all probability distributions P over the (finite) derivation trees t that the
	// grammar can generate from `start`, find the one maximizing the Shannon entropy
	//        H(P) = - sum_t P(t) log P(t)
	// subject to the single moment constraint E_P[cost(t)] = L, where cost(t) = sum of w(rule) over
	// the nodes of t (w = 1 gives the node count, i.e. Node::count()).
	//
	// SOLUTION. By the standard Lagrangian/Gibbs argument, the maximizer is
	//        P*(t) = exp(-lambda * cost(t)) / Z_start(lambda),
	// with lambda chosen so the constraint holds. Because exp(-lambda*cost(t)) factorizes over
	// nodes, P* is itself a PCFG: give rule r weight u_r = exp(-lambda w_r); the partition functions
	//        Z_i = sum_{r in R_i} u_r * prod_{c in children(r)} Z_c          (least fixed point)
	// and the rule probabilities
	//        p_r = u_r * prod_{c in children(r)} Z_c / Z_i
	// generate exactly P* (by telescoping: the product of p_r over a tree is u(t)/Z_start). So "max
	// entropy over programs" and "max entropy over PCFG parameters" coincide, and the answer is
	// unique. Note this is NOT "uniform over rules within each nonterminal": a rule whose children
	// can be expanded in more ways (larger Z_c) gets more mass, which is exactly what makes the
	// distribution over *whole programs* as flat as possible given the length budget.
	//
	// The tree entropy of a PCFG is H = sum_i v_i H_i, with v_i = expected number of expansions of
	// nonterminal i (v^T = e_start^T (I - M)^{-1}) and H_i the entropy of the rule choice at i.
	// For P* it also equals lambda * L + log Z_start; we compute both and require they agree, which
	// certifies the solution.
	//
	// ALGORITHM.
	//   * m_i = minimum tree cost from i (Knuth's generalization of Dijkstra, via value iteration).
	//     Partition functions are rescaled Z_i = exp(-lambda m_i) Zhat_i so that Zhat_i >= 1 and no
	//     underflow occurs for large lambda.
	//   * For a given lambda, Zhat is the least fixed point of a monotone polynomial system; we
	//     find it by Newton's method from 0, which converges monotonically to the least fixed point
	//     (Esparza, Kiefer & Luttenberger 2010). We do NOT rely on that theorem for correctness: a
	//     fixed point x of a monotone convex system with rho(F'(x)) < 1 is necessarily the least one
	//     (convexity gives (I - F'(x))(x - mu) <= 0, and (I - F'(x))^{-1} >= 0). The induced PCFG's
	//     mean matrix is diag(Zhat)^{-1} F'(Zhat) diag(Zhat), so it has the same spectral radius; we
	//     verify rho < 1 with solve_block(), which simultaneously gives E(lambda).
	//   * E(lambda) is strictly decreasing (dE/dlambda = -Var(cost) < 0) on (lambda_c, inf), tends to
	//     the minimum cost as lambda -> inf, and to +inf (recursive grammars, lambda -> lambda_c+) or to
	//     the maximum cost (non-recursive grammars, lambda -> -inf). We bracket and bisect lambda to
	//     double precision, treating lambda <= lambda_c (no finite / subcritical solution) as "E too big".
	//
	// SCOPE / SEMANTICS (read these):
	//   * Rules that currently have p == 0 are treated as disabled and stay at 0. All rules with p > 0
	//     are treated as available; their current values are otherwise ignored (pure max entropy).
	//   * Only nonterminals reachable from `start` (through p > 0 rules) are modified; other
	//     nonterminals do not affect trees from `start` and are left untouched.
	//   * Costs must be strictly positive for the available rules reachable from start (otherwise
	//     infinitely many trees can share a cost and the problem can be ill-posed).
	//   * The target must satisfy min_cost < L < max_cost (max_cost = inf if any reachable
	//     nonterminal is recursive).
	//   * Rule probabilities are written so p, logp, Grammar::Z and Grammar::logZ stay consistent
	//     (as change_probability does). Rules are NOT re-sorted: existing Nodes hold Rule* pointers
	//     into the rule vectors, so re-sorting would silently corrupt them. Because Rule ordering
	//     uses p as a secondary key, the within-nonterminal order may afterwards differ from the
	//     order a freshly-built grammar would have (terminals still come first).
	//   * Strong exception guarantee: the grammar is modified only after every check has passed.

	struct MaxEntResult {
		nonterminal_t       start = 0;
		double              target_length   = std::numeric_limits<double>::quiet_NaN();
		double              achieved_length = std::numeric_limits<double>::quiet_NaN(); ///< E[cost] under the new probabilities
		double              lambda          = std::numeric_limits<double>::quiet_NaN(); ///< Lagrange multiplier (cost "temperature")
		double              min_length      = std::numeric_limits<double>::quiet_NaN(); ///< min tree cost from start
		double              max_length      = std::numeric_limits<double>::quiet_NaN(); ///< max tree cost (inf if recursive)
		double              entropy         = std::numeric_limits<double>::quiet_NaN(); ///< H of the tree distribution, nats
		double              log_partition   = std::numeric_limits<double>::quiet_NaN(); ///< log Z_start(lambda) = log sum_t exp(-lambda cost(t))
		double              spectral_radius = std::numeric_limits<double>::quiet_NaN(); ///< rho of the new mean matrix (< 1)
		std::vector<bool>   modified;        ///< modified[i]: rules of nonterminal i were set
		std::vector<double> expected_visits; ///< v_i: expected number of expansions of nonterminal i per tree
	};

	namespace detail {

		/// Per-nonterminal local view of the available (p > 0) rules of the reachable block.
		struct LocalRule {
			const Rule*         rule;
			double              w;        // cost
			std::vector<size_t> kids;     // local indices of children
			double              excess;   // w + sum m_kids - m_parent >= 0
		};

		struct ZSolution {
			bool                             ok = false;  // fixed point found and certified subcritical
			double                           E = std::numeric_limits<double>::infinity();
			double                           rho = std::numeric_limits<double>::quiet_NaN();
			Eigen::VectorXd                  Zhat;
			std::vector<std::vector<double>> p;          // p[a][k] for local rule k of local nonterminal a
		};

		/// For fixed lambda: least fixed point of the rescaled partition-function system, the induced
		/// PCFG, and its expected cost. ok == false means "no finite, subcritical solution" (lambda at
		/// or below the critical value, or numerically indistinguishable from it).
		inline ZSolution solve_lambda(const std::vector<std::vector<LocalRule>>& R, const double lambda, const double critical_tolerance) {
			const size_t K = R.size();
			ZSolution out;

			// rule weights in rescaled units: a_r = exp(-lambda * excess_r)
			std::vector<std::vector<double>> a(K);
			for(size_t i=0;i<K;i++) {
				for(const auto& r : R[i]) {
					const double v = std::exp(-lambda * r.excess);
					if(not std::isfinite(v)) return out; // overflow (only possible for very negative lambda)
					a[i].push_back(v);
				}
			}

			auto prod_except = [](const Eigen::VectorXd& Z, const std::vector<size_t>& kids, size_t skip) {
				double pr = 1.0;
				for(size_t l=0;l<kids.size();l++) if(l != skip) pr *= Z(kids[l]);
				return pr;
			};
			const size_t NONE = std::numeric_limits<size_t>::max();

			Eigen::VectorXd Z = Eigen::VectorXd::Zero(K), F(K);
			Eigen::MatrixXd J(K, K);
			bool converged = false;
			double last_step = std::numeric_limits<double>::infinity();
			for(int it=0; it<2000; it++) {
				F.setZero(); J.setZero();
				for(size_t i=0;i<K;i++) {
					for(size_t k=0;k<R[i].size();k++) {
						const auto& kids = R[i][k].kids;
						F(i) += a[i][k] * prod_except(Z, kids, NONE);
						for(size_t l=0;l<kids.size();l++)
							J(i, kids[l]) += a[i][k] * prod_except(Z, kids, l);
					}
				}
				const Eigen::VectorXd G = F - Z;
				const double zn    = std::max(1.0, Z.lpNorm<Eigen::Infinity>());
				const double resid = G.lpNorm<Eigen::Infinity>();
				// Converged: residual at rounding level, or the Newton step has stalled at rounding
				// level with a small residual (F is a sum of many positive terms, so its own rounding
				// error can exceed 1e-15*|Z|).
				if(it > 0 and (resid <= 1e-15 * zn or (last_step <= 1e-13 * zn and resid <= 1e-11 * zn))) { converged = true; break; }

				Eigen::FullPivLU<Eigen::MatrixXd> lu(Eigen::MatrixXd::Identity(K,K) - J);
				if(not lu.isInvertible()) return out;
				const Eigen::VectorXd dZ = lu.solve(G);
				if(not dZ.allFinite()) return out;
				// Newton from below is monotone nondecreasing; a clearly negative step means we have
				// left the region where the theory holds (lambda below critical / numerical breakdown).
				if(dZ.minCoeff() < -1e-9 * zn) return out;
				Z += dZ;
				last_step = dZ.lpNorm<Eigen::Infinity>();
				if(not Z.allFinite() or Z.maxCoeff() > 1e150) return out; // diverging: lambda <= lambda_c
			}
			if(not converged) return out;
			for(size_t i=0;i<K;i++) if(not (Z(i) > 0.0)) return out;

			// induced PCFG
			out.p.assign(K, {});
			Eigen::MatrixXd M = Eigen::MatrixXd::Zero(K, K);
			Eigen::VectorXd c = Eigen::VectorXd::Zero(K);
			for(size_t i=0;i<K;i++) {
				double s = 0.0;
				for(size_t k=0;k<R[i].size();k++) {
					const double pk = a[i][k] * prod_except(Z, R[i][k].kids, NONE) / Z(i);
					out.p[i].push_back(pk);
					s += pk;
				}
				// at an exact fixed point s == 1; normalize away rounding but require it to be small
				if(not (std::abs(s - 1.0) <= 1e-10)) return out;
				for(size_t k=0;k<R[i].size();k++) {
					out.p[i][k] /= s;
					c(i) += out.p[i][k] * R[i][k].w;
					for(size_t kid : R[i][k].kids) M(i, kid) += out.p[i][k];
				}
			}

			const BlockSolution sol = solve_block(M, c, critical_tolerance);
			out.rho = sol.rho;
			if(not sol.finite) return out; // fixed point is not the least one / critical
			out.E    = sol.x(0);           // local index 0 is the start symbol
			out.Zhat = Z;
			out.ok   = true;
			return out;
		}
	} // namespace detail

	/**
	 * @brief Set the rule probabilities of `grammar` to the maximum-entropy PCFG over programs from
	 *        `start` whose expected (cost-weighted) length is `target_length`. See the long comment
	 *        above for the math, the algorithm, and the exact scope of what is modified.
	 *
	 * @param grammar        Fleet grammar to modify in place.
	 * @param target_length  desired E[cost(tree)]; with UnitCost this is E[Node::count()], the same
	 *                       quantity analyze()/expected_length() report.
	 * @param start          root nonterminal.
	 * @param cost           strictly positive per-rule cost functor (default: 1 per node).
	 * @param rel_tol        required relative accuracy |E - L| <= rel_tol * L. lambda is bisected to
	 *                       full double precision; for very large targets (rho very close to 1) E is so
	 *                       sensitive to lambda that a looser tolerance may be needed -- the function
	 *                       throws rather than silently returning an inaccurate result.
	 * @param critical_tolerance passed to the subcriticality check (see analyze()).
	 *
	 * @return MaxEntResult with lambda, achieved length, entropy, etc.
	 * @throws std::invalid_argument if the target is outside (min_cost, max_cost), costs are not > 0, etc.
	 * @throws std::runtime_error    on unreachable-but-unproductive nonterminals or failed numerical checks.
	 */
	template<typename G, RuleCost CostF = UnitCost>
	MaxEntResult set_max_entropy_probabilities(G& grammar, const double target_length, const nonterminal_t start,
	                                           CostF cost = CostF{}, const double rel_tol = 1e-9,
	                                           const double critical_tolerance = 1e-10) {
		constexpr size_t N = G::N_NTs;
		const double inf = std::numeric_limits<double>::infinity();

		if(not std::isfinite(target_length) or target_length <= 0.0)
			throw std::invalid_argument("*** target_length must be finite and > 0");
		if(not (rel_tol > 0.0 and rel_tol < 1.0))
			throw std::invalid_argument("*** rel_tol must be in (0,1)");
		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))
			throw std::invalid_argument("*** critical_tolerance must be in [0,1)");
		if(static_cast<size_t>(start) >= N)
			throw std::invalid_argument("*** start nonterminal " + std::to_string(start) + " out of range (N_NTs=" + std::to_string(N) + ")");

		// --- validate and collect available rules, reachability ---------------------------------
		std::vector<std::vector<const Rule*>> avail(N);
		for(size_t i=0;i<N;i++) {
			for(const Rule& r : grammar.rules[i]) {
				if(static_cast<size_t>(r.nt) != i)
					throw std::invalid_argument("*** Rule stored under nonterminal " + std::to_string(i) + " has nt=" + std::to_string(r.nt) + ": " + r.string());
				if(not std::isfinite(r.p) or r.p < 0.0)
					throw std::invalid_argument("*** Rule probability must be finite and >= 0: " + r.string());
				for(const nonterminal_t t : r.get_child_types())
					if(static_cast<size_t>(t) >= N) throw std::invalid_argument("*** Child type out of range in rule " + r.string());
				if(r.p > 0.0) avail[i].push_back(&r);
			}
		}

		std::vector<bool> reach(N, false);
		{
			std::deque<size_t> q{static_cast<size_t>(start)};
			reach[start] = true;
			while(not q.empty()) {
				const size_t i = q.front(); q.pop_front();
				for(const Rule* r : avail[i])
					for(const nonterminal_t t : r->get_child_types())
						if(not reach[t]) { reach[t] = true; q.push_back(t); }
			}
		}

		// local indexing with start at local index 0
		std::vector<size_t> idx{static_cast<size_t>(start)};
		for(size_t i=0;i<N;i++) if(reach[i] and i != start) idx.push_back(i);
		const size_t K = idx.size();
		std::vector<size_t> loc(N, std::numeric_limits<size_t>::max());
		for(size_t a=0;a<K;a++) loc[idx[a]] = a;

		std::vector<std::vector<detail::LocalRule>> R(K);
		for(size_t a=0;a<K;a++) {
			if(avail[idx[a]].empty())
				throw std::runtime_error("*** Nonterminal " + std::to_string(idx[a]) + " is reachable from start but has no rule with positive probability.");
			for(const Rule* r : avail[idx[a]]) {
				const double w = static_cast<double>(cost(*r));
				if(not std::isfinite(w) or w <= 0.0)
					throw std::invalid_argument("*** set_max_entropy_probabilities requires finite costs > 0; got " + std::to_string(w) + " for " + r->string());
				detail::LocalRule lr{r, w, {}, 0.0};
				for(const nonterminal_t t : r->get_child_types()) lr.kids.push_back(loc[t]);
				R[a].push_back(std::move(lr));
			}
		}

		// --- minimum tree cost m (value iteration; min trees have height <= K, so K+1 passes suffice) ---
		std::vector<double> m(K, inf);
		for(size_t sweep=0; sweep<=K+1; sweep++) {
			bool changed = false;
			for(size_t a=0;a<K;a++) {
				for(const auto& r : R[a]) {
					double v = r.w;
					for(size_t kid : r.kids) v += m[kid];
					if(v < m[a]) { m[a] = v; changed = true; }
				}
			}
			if(not changed) break;
			if(sweep == K+1) throw std::runtime_error("*** Internal error: minimum-cost iteration did not converge.");
		}
		for(size_t a=0;a<K;a++)
			if(not std::isfinite(m[a]))
				throw std::runtime_error("*** Nonterminal " + std::to_string(idx[a]) + " is reachable from start but cannot generate any finite tree.");
		for(size_t a=0;a<K;a++) {
			for(auto& r : R[a]) {
				double e = r.w - m[a];
				for(size_t kid : r.kids) e += m[kid];
				const double scale = r.w + m[a];
				if(e < -1e-12 * scale) throw std::runtime_error("*** Internal error: negative excess cost.");
				r.excess = std::max(0.0, e);
			}
		}

		// --- maximum tree cost (inf if the reachable rule graph has a cycle) ---------------------
		double max_cost = inf;
		{
			// DFS with colors over local nonterminals; compute longest-tree cost if acyclic
			std::vector<int> color(K, 0); // 0 new, 1 on stack, 2 done
			std::vector<double> mx(K, 0.0);
			bool cyclic = false;
			std::function<void(size_t)> dfs = [&](size_t a) {
				color[a] = 1;
				double best = -inf;
				for(const auto& r : R[a]) {
					double v = r.w;
					for(size_t kid : r.kids) {
						if(color[kid] == 1) { cyclic = true; }
						else if(color[kid] == 0) dfs(kid);
						v += mx[kid];
					}
					best = std::max(best, v);
				}
				mx[a] = best;
				color[a] = 2;
			};
			dfs(0);
			if(not cyclic) max_cost = mx[0];
		}

		const double min_cost = m[0];
		if(not (target_length > min_cost * (1.0 + 1e-12)))
			throw std::invalid_argument("*** target_length " + std::to_string(target_length) + " must exceed the minimum tree cost " + std::to_string(min_cost));
		if(not (target_length < max_cost * (1.0 - 1e-12)))
			throw std::invalid_argument("*** target_length " + std::to_string(target_length) + " must be below the maximum tree cost " + std::to_string(max_cost) + " (grammar is not recursive from start)");

		// --- bracket and bisect lambda: f(lambda) = "E(lambda) > L or infeasible" is monotone ---
		auto eval = [&](double lam) { return detail::solve_lambda(R, lam, critical_tolerance); };
		auto too_big = [&](const detail::ZSolution& s) { return (not s.ok) or s.E > target_length; };

		double hi = 1.0;
		detail::ZSolution shi = eval(hi);
		for(int k=0; too_big(shi); k++) {
			if(k > 60) throw std::runtime_error("*** Could not find lambda giving expected length below the target.");
			hi = 2.0*hi + 1.0;
			shi = eval(hi);
		}
		double lo = hi - 1.0;
		detail::ZSolution slo = eval(lo);
		for(int k=0; not too_big(slo); k++) {
			if(k > 60) throw std::runtime_error("*** Could not find lambda giving expected length above the target.");
			lo = hi - 2.0*(hi - lo);
			slo = eval(lo);
		}
		// invariant: too_big(lo) and not too_big(hi), lo < hi
		for(int it=0; it<400; it++) {
			const double mid = lo + 0.5*(hi - lo);
			if(mid <= lo or mid >= hi) break; // adjacent doubles
			detail::ZSolution sm = eval(mid);
			if(too_big(sm)) { lo = mid; slo = std::move(sm); }
			else            { hi = mid; shi = std::move(sm); }
			if(std::abs(shi.E - target_length) <= 1e-3 * rel_tol * target_length) break;
		}

		// pick the closer feasible endpoint
		detail::ZSolution best = std::move(shi);
		double lambda = hi;
		if(slo.ok and std::abs(slo.E - target_length) < std::abs(best.E - target_length)) { best = std::move(slo); lambda = lo; }
		if(not best.ok or not (std::abs(best.E - target_length) <= rel_tol * target_length))
			throw std::runtime_error("*** Could not attain target length " + std::to_string(target_length) + " to rel_tol=" + std::to_string(rel_tol) +
			                         " (closest achieved " + std::to_string(best.E) + " at lambda=" + std::to_string(lambda) +
			                         "); E is extremely sensitive to lambda here -- loosen rel_tol or lower the target.");

		// --- entropy two ways (certification of the Gibbs form) -------------------------------
		Eigen::MatrixXd M = Eigen::MatrixXd::Zero(K, K);
		Eigen::VectorXd Hloc = Eigen::VectorXd::Zero(K);
		for(size_t a=0;a<K;a++) {
			for(size_t k=0;k<R[a].size();k++) {
				const double pk = best.p[a][k];
				if(pk > 0.0) Hloc(a) -= pk * std::log(pk);
				for(size_t kid : R[a][k].kids) M(a, kid) += pk;
			}
		}
		Eigen::VectorXd e0 = Eigen::VectorXd::Zero(K); e0(0) = 1.0;
		Eigen::FullPivLU<Eigen::MatrixXd> luT((Eigen::MatrixXd::Identity(K,K) - M).transpose());
		const Eigen::VectorXd v = luT.solve(e0);
		if(not v.allFinite() or v.minCoeff() < -1e-9 or v(0) < 1.0 - 1e-9) // v >= 0 and v_start >= 1 (the root itself)
			throw std::runtime_error("*** Internal error: invalid expected-visit vector.");
		const double H_visits = v.dot(Hloc);
		const double logZ     = -lambda * min_cost + std::log(best.Zhat(0));
		const double H_gibbs  = lambda * best.E + logZ;
		if(not (std::abs(H_visits - H_gibbs) <= 1e-7 * (1.0 + std::abs(H_visits))))
			throw std::runtime_error("*** Entropy certification failed (" + std::to_string(H_visits) + " vs " + std::to_string(H_gibbs) + ").");

		// --- commit (no throwing operations below) ---------------------------------------------
		MaxEntResult out;
		out.start = start;
		out.target_length = target_length;
		out.achieved_length = best.E;
		out.lambda = lambda;
		out.min_length = min_cost;
		out.max_length = max_cost;
		out.entropy = H_visits;
		out.log_partition = logZ;
		out.spectral_radius = best.rho;
		out.modified.assign(N, false);
		out.expected_visits.assign(N, 0.0);

		for(size_t a=0;a<K;a++) {
			const size_t i = idx[a];
			out.modified[i] = true;
			out.expected_visits[i] = v(a);
			// map available rules -> new probability; everything else in rules[i] had p == 0 and stays 0
			double Zi = 0.0;
			for(Rule& r : grammar.rules[i]) {
				double np = 0.0;
				for(size_t k=0;k<R[a].size();k++) if(R[a][k].rule == &r) { np = best.p[a][k]; break; }
				r.p    = np;
				r.logp = std::log(np); // log(0) = -inf, as Fleet stores for zero-probability rules
				Zi    += np;
			}
			grammar.Z[i]    = Zi;
			grammar.logZ[i] = std::log(Zi);
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
	//  DOES THE GRAMMAR ASSIGN PROBABILITY TO INFINITE TREES?  (tightness / consistency)
	//
	// =========================================================================================
	//
	// A PCFG is "tight" (consistent) from `start` if P(the generated tree is finite) = 1. This is
	// NOT the same as finite expected length: a critical grammar such as S -> f(S,S) [1/2] | x [1/2]
	// is tight (every tree is finite a.s.) yet has E[size] = infinity.
	//
	// Let q_i = P(tree from i is finite). q is the LEAST fixed point of q_i = sum_r p_r prod_c q_c.
	// Exact criterion (multitype branching processes; cf. Etessami & Yannakakis 2009, Harris 1963):
	//   (a) If start reaches (via p > 0 rules) a nonterminal that cannot derive ANY finite tree
	//       ("unproductive"), then q_start < 1. This is decided combinatorially, no floating point.
	//   (b) Otherwise every reachable nonterminal is productive, and q_start < 1 iff some reachable
	//       strongly connected component C of the nonterminal dependency graph is supercritical:
	//       rho(M_C) > 1, where M_C is the mean matrix restricted to C. (Critical components are fine:
	//       a critical, productive -- hence non-singular -- class dies out a.s., so it produces finitely
	//       many individuals and therefore finitely many "immigrants" into downstream classes; induct
	//       over the DAG of components.)
	//
	// FLOATING POINT. Part (b) compares rho to 1. Probabilities are doubles and rho is computed
	// numerically, so a component with |rho - 1| <= critical_tolerance is reported as near_critical
	// and treated as critical (tight). If the true rho is 1 + d with tiny d > 0, the true probability
	// of an infinite tree is O(d) -- i.e. at the level of floating-point noise. Check near_critical
	// whenever the distinction matters. We also report q itself, computed by Newton's method from 0
	// (monotone convergence to the least fixed point), as prob_infinite = 1 - q_start.
	//
	// As in analyze(): only p > 0 rules count, unreachable nonterminals are ignored, and a reachable
	// nonterminal with no positive-probability rule throws (Grammar::generate would fail there).

	struct TightnessResult {
		nonterminal_t       start = 0;
		bool                infinite_trees_possible = false; ///< THE ANSWER: P(infinite tree) > 0 (see near_critical)
		bool                unproductive_reachable  = false; ///< reason (a): exact, combinatorial
		bool                supercritical_reachable = false; ///< reason (b): some reachable component has rho > 1 + tol
		bool                near_critical           = false; ///< some reachable recursive component has |rho - 1| <= tol
		double              max_component_rho       = 0.0;   ///< largest rho over reachable components (0 if none recursive)
		/// P(infinite tree) = 1 - q_start. Exactly 0 whenever infinite_trees_possible is false (the
		/// theorem gives q = 1 exactly there; at criticality numerics alone can only resolve 1 - q to
		/// ~sqrt(machine eps) ~ 1e-8 because F(q) - q ~ (1-q)^2, and to ~1e-4 for two chained critical
		/// classes). Otherwise the numerical least fixed point.
		double              prob_infinite = std::numeric_limits<double>::quiet_NaN();
		double              prob_infinite_numerical = std::numeric_limits<double>::quiet_NaN(); ///< raw Newton estimate, for diagnostics
		std::vector<bool>   reachable;
		std::vector<bool>   productive;        ///< among reachable nonterminals; false elsewhere
		std::vector<bool>   tight_by_nt;       ///< reachable i whose own trees are finite a.s. (cannot reach an unproductive or supercritical nonterminal)
		std::vector<double> prob_finite_by_nt; ///< q_i for reachable i: exactly 1 if tight_by_nt[i], exactly 0 if unproductive, else numerical; NaN unreachable
		std::vector<double> component_rho_by_nt; ///< rho of the component containing i (0 if non-recursive); NaN unreachable
	};

	template<typename G>
	TightnessResult analyze_tightness(const G& grammar, const nonterminal_t start, const double critical_tolerance = 1e-10) {
		constexpr size_t N = G::N_NTs;
		const double nan = std::numeric_limits<double>::quiet_NaN();
		if(static_cast<size_t>(start) >= N)
			throw std::invalid_argument("*** start nonterminal " + std::to_string(start) + " out of range (N_NTs=" + std::to_string(N) + ")");
		if(not (critical_tolerance >= 0.0 and critical_tolerance < 1.0))
			throw std::invalid_argument("*** critical_tolerance must be in [0,1)");

		// --- available rules, normalized probabilities, reachability ------------------------------
		std::vector<std::vector<const Rule*>> avail(N);
		std::vector<double> Z(N, 0.0);
		for(size_t i=0;i<N;i++) {
			for(const Rule& r : grammar.rules[i]) {
				if(static_cast<size_t>(r.nt) != i)
					throw std::invalid_argument("*** Rule stored under nonterminal " + std::to_string(i) + " has nt=" + std::to_string(r.nt) + ": " + r.string());
				if(not std::isfinite(r.p) or r.p < 0.0)
					throw std::invalid_argument("*** Rule probability must be finite and >= 0: " + r.string());
				for(const nonterminal_t t : r.get_child_types())
					if(static_cast<size_t>(t) >= N) throw std::invalid_argument("*** Child type out of range in rule " + r.string());
				if(r.p > 0.0) { avail[i].push_back(&r); Z[i] += r.p; }
			}
			if(not std::isfinite(Z[i])) throw std::invalid_argument("*** Sum of rule probabilities overflowed for nonterminal " + std::to_string(i));
		}

		TightnessResult out;
		out.start = start;
		out.reachable.assign(N, false);
		out.productive.assign(N, false);
		out.prob_finite_by_nt.assign(N, nan);
		out.component_rho_by_nt.assign(N, nan);
		{
			std::deque<size_t> q{static_cast<size_t>(start)};
			out.reachable[start] = true;
			while(not q.empty()) {
				const size_t i = q.front(); q.pop_front();
				for(const Rule* r : avail[i])
					for(const nonterminal_t t : r->get_child_types())
						if(not out.reachable[t]) { out.reachable[t] = true; q.push_back(t); }
			}
		}
		std::vector<size_t> idx{static_cast<size_t>(start)};
		for(size_t i=0;i<N;i++) if(out.reachable[i] and i != start) idx.push_back(i);
		const size_t K = idx.size();
		std::vector<size_t> loc(N, std::numeric_limits<size_t>::max());
		for(size_t a=0;a<K;a++) {
			loc[idx[a]] = a;
			if(avail[idx[a]].empty())
				throw std::runtime_error("*** Nonterminal " + std::to_string(idx[a]) + " is reachable from start " + std::to_string(start) +
				                         " but has no rule with positive probability; trees from start cannot be generated.");
		}

		// --- (a) productivity: least fixed point of "some rule has all children productive" -------
		std::vector<bool> prod(K, false);
		for(bool changed = true; changed; ) {
			changed = false;
			for(size_t a=0;a<K;a++) {
				if(prod[a]) continue;
				for(const Rule* r : avail[idx[a]]) {
					bool all = true;
					for(const nonterminal_t t : r->get_child_types()) if(not prod[loc[t]]) { all = false; break; }
					if(all) { prod[a] = true; changed = true; break; }
				}
			}
		}
		for(size_t a=0;a<K;a++) {
			out.productive[idx[a]] = prod[a];
			if(not prod[a]) out.unproductive_reachable = true;
		}

		// --- mean matrix on the reachable block, and strongly connected components (Tarjan) --------
		Eigen::MatrixXd M = Eigen::MatrixXd::Zero(K, K);
		std::vector<std::vector<size_t>> adj(K);
		for(size_t a=0;a<K;a++) {
			for(const Rule* r : avail[idx[a]]) {
				for(const nonterminal_t t : r->get_child_types()) {
					M(a, loc[t]) += r->p / Z[idx[a]];
					adj[a].push_back(loc[t]);
				}
			}
		}
		std::vector<int> comp(K, -1), low(K, 0), num(K, -1);
		std::vector<bool> onstack(K, false);
		std::vector<size_t> stack;
		int counter = 0, ncomp = 0;
		std::function<void(size_t)> tarjan = [&](size_t v) {
			num[v] = low[v] = counter++;
			stack.push_back(v); onstack[v] = true;
			for(size_t w : adj[v]) {
				if(num[w] == -1) { tarjan(w); low[v] = std::min(low[v], low[w]); }
				else if(onstack[w]) low[v] = std::min(low[v], num[w]);
			}
			if(low[v] == num[v]) {
				size_t w;
				do { w = stack.back(); stack.pop_back(); onstack[w] = false; comp[w] = ncomp; } while(w != v);
				ncomp++;
			}
		};
		for(size_t a=0;a<K;a++) if(num[a] == -1) tarjan(a);

		// --- (b) spectral radius of each recursive component ---------------------------------------
		for(int cpt=0; cpt<ncomp; cpt++) {
			std::vector<size_t> members;
			for(size_t a=0;a<K;a++) if(comp[a] == cpt) members.push_back(a);
			const Eigen::Index m = static_cast<Eigen::Index>(members.size());
			Eigen::MatrixXd B(m, m);
			for(Eigen::Index x=0;x<m;x++) for(Eigen::Index y=0;y<m;y++) B(x,y) = M(members[x], members[y]);
			double rho = 0.0;
			if(B.sum() > 0.0) { // recursive component (has an internal edge)
				Eigen::EigenSolver<Eigen::MatrixXd> es(B, false);
				if(es.info() != Eigen::Success) throw std::runtime_error("*** Eigenvalue computation failed in analyze_tightness.");
				for(Eigen::Index k=0;k<m;k++) rho = std::max(rho, std::abs(es.eigenvalues()(k)));
				const double rs_max = B.rowwise().sum().maxCoeff(), rs_min = B.rowwise().sum().minCoeff();
				// Perron-Frobenius bounds for an irreducible nonnegative matrix: min row sum <= rho <= max row sum
				if(not std::isfinite(rho) or rho > rs_max*(1+1e-9)+1e-12 or rho < rs_min*(1-1e-9)-1e-12)
					throw std::runtime_error("*** Component spectral radius " + std::to_string(rho) + " violates Perron-Frobenius row-sum bounds.");
				if(rho > 1.0 + critical_tolerance)                 out.supercritical_reachable = true;
				else if(std::abs(rho - 1.0) <= critical_tolerance) out.near_critical = true;
			}
			out.max_component_rho = std::max(out.max_component_rho, rho);
			for(size_t a : members) out.component_rho_by_nt[idx[a]] = rho;
		}

		out.infinite_trees_possible = out.unproductive_reachable or out.supercritical_reachable;

		// per-nonterminal tightness: i is tight iff it cannot reach a "bad" nonterminal (unproductive,
		// or in a component with rho > 1 + tol). bad propagates backwards along edges.
		std::vector<bool> bad(K, false);
		for(size_t a=0;a<K;a++)
			if(not prod[a] or out.component_rho_by_nt[idx[a]] > 1.0 + critical_tolerance) bad[a] = true;
		for(bool changed = true; changed; ) {
			changed = false;
			for(size_t a=0;a<K;a++) {
				if(bad[a]) continue;
				for(size_t b : adj[a]) if(bad[b]) { bad[a] = true; changed = true; break; }
			}
		}
		out.tight_by_nt.assign(N, false);
		for(size_t a=0;a<K;a++) out.tight_by_nt[idx[a]] = not bad[a];
		if(out.tight_by_nt[start] == out.infinite_trees_possible)
			throw std::runtime_error("*** analyze_tightness internal inconsistency between global and per-nonterminal decisions.");

		// --- numerical termination probabilities q (least fixed point; Newton from 0) --------------
		{
			Eigen::VectorXd q = Eigen::VectorXd::Zero(K), F(K);
			Eigen::MatrixXd J(K, K);
			for(int it=0; it<20000; it++) {
				F.setZero(); J.setZero();
				for(size_t a=0;a<K;a++) {
					for(const Rule* r : avail[idx[a]]) {
						const double pr = r->p / Z[idx[a]];
						const auto& ch = r->get_child_types();
						double all = pr;
						for(const nonterminal_t t : ch) all *= q(loc[t]);
						F(a) += all;
						for(size_t l=0;l<ch.size();l++) {
							double d = pr;
							for(size_t k=0;k<ch.size();k++) if(k != l) d *= q(loc[ch[k]]);
							J(a, loc[ch[l]]) += d;
						}
					}
				}
				Eigen::FullPivLU<Eigen::MatrixXd> lu(Eigen::MatrixXd::Identity(K,K) - J);
				lu.setThreshold(1e-300); // near criticality I - J is legitimately nearly singular; still use Newton
				Eigen::VectorXd step = (F - q);
				if(lu.isInvertible()) {
					const Eigen::VectorXd dq = lu.solve(F - q);
					if(dq.allFinite() and dq.minCoeff() >= -1e-12) step = dq; // Newton (monotone from below)
				}
				// fall back to a plain fixed-point step (also monotone) if Newton is unusable
				q = (q + step).cwiseMax(0.0).cwiseMin(1.0);
				if(step.lpNorm<Eigen::Infinity>() <= 1e-16) break;
			}
			out.prob_infinite_numerical = 1.0 - (prod[0] ? q(0) : 0.0);
			for(size_t a=0;a<K;a++)
				out.prob_finite_by_nt[idx[a]] = (not prod[a] ? 0.0 : (out.tight_by_nt[idx[a]] ? 1.0 : q(a)));
			out.prob_infinite = 1.0 - out.prob_finite_by_nt[start];

			// Internal consistency check against the numerics, ONLY where numerics are well conditioned:
			// with no (near-)critical component, Newton converges quadratically to q = 1. Near
			// criticality the numerics are fundamentally limited -- 1 - q is resolvable only to
			// ~eps^(1/2) for one critical class, and to ~eps^(1/2^k) for a chain of k critical classes
			// feeding each other (e.g. ~1e-4 for k = 2) -- which is why the decision is made exactly
			// rather than from q.
			if(not out.near_critical) for(size_t a=0;a<K;a++) {
				if(out.tight_by_nt[idx[a]] and not (1.0 - q(a) <= 1e-9))
					throw std::runtime_error("*** analyze_tightness internal inconsistency: nonterminal " + std::to_string(idx[a]) +
					                         " decided tight but numerical P(infinite) = " + std::to_string(1.0 - q(a)));
			}
		}
		return out;
	}

	template<typename G>
	TightnessResult analyze_tightness(const G& grammar) {
		return analyze_tightness(grammar, G::template nt<typename G::output_t>());
	}

	/// True iff trees generated from `start` are infinite with positive probability (the PCFG is not
	/// tight). Exactly-critical components count as tight; see TightnessResult::near_critical.
	template<typename G>
	bool assigns_probability_to_infinite_trees(const G& grammar, const nonterminal_t start, const double critical_tolerance = 1e-10) {
		return analyze_tightness(grammar, start, critical_tolerance).infinite_trees_possible;
	}

	template<typename G>
	bool assigns_probability_to_infinite_trees(const G& grammar) {
		return analyze_tightness(grammar).infinite_trees_possible;
	}

} // namespace GrammarExpectedLength
