#pragma once

#include <vector>
#include <array>
#include <optional>
#include <limits>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "IntegerizedStack.h" // for enumerationidx_t

/**
 * Thrown when an integer (or a tree's code) is outside the range of a nonterminal's enumeration:
 * z >= count(nt) for a finite nonterminal, or any z for a nonterminal with no finite trees.
 */
struct EnumerationOutOfRangeException : std::out_of_range {
	using std::out_of_range::out_of_range;
};

/**
 * Thrown when an exact count or a tree's code does not fit in enumerationidx_t. This is
 * NEVER silent: every multiply/add on an index is overflow-checked.
 */
struct EnumerationOverflowException : std::overflow_error {
	using std::overflow_error::overflow_error;
};

/**
 * @class GeneralEnumeration
 * @author piantado
 * @file GeneralEnumeration.h
 * @brief A bijection between integers and the trees of a grammar, using pairing functions like
 *        BasicEnumeration, but correct for ANY grammar:
 *          - nonterminals with only finitely many trees (enumerated by [0, count(nt)) ),
 *          - nonterminals with no terminal rules (BasicEnumeration recurses forever on these),
 *          - unproductive nonterminals/rules (rules that can never finish a tree are skipped).
 *
 *  ANALYSIS (done once, in the constructor). For each nonterminal nt:
 *    productive(nt)  nt has at least one finite tree (least fixed point).
 *    useful rule     a rule all of whose children are productive. Only useful rules can occur
 *                    in a complete tree, so only they are enumerated.
 *    infinite(nt)    nt is productive and, in the graph  nt -> child type  over useful rules,
 *                    nt can reach a cycle. (Pumping: such a cycle can be repeated any number of
 *                    times; without one, trees have bounded height and so are finitely many.)
 *    count(nt)       for finite nt, the exact number of trees (sum over rules of the product of
 *                    child counts; the useful-rule graph below a finite nt is a DAG).
 *
 *  A useful rule is "finite" if all of its children are finite nonterminals (terminals included,
 *  with count 1) and "infinite" otherwise. A finite rule r has count(r) = prod_i count(child_i).
 *
 *  ENCODING of nt (b = sum of count(r) over the finite rules of nt, in grammar order, so the
 *  terminals come first exactly as in BasicEnumeration):
 *    z in [0, b)    the "base block": z picks a finite rule r (by cumulative offsets), and the
 *                   remainder is a mixed-radix number giving each child's index in [0, count(child)).
 *                   For a finite nt the base block is all of [0, count(nt)).
 *    z >= b         (infinite nt only) let z' = z - b and m = #infinite rules of nt. The rule is
 *                   infinite_rules[z' mod m]; the rest v = z' div m is split as: mixed-radix digits
 *                   for the finite children (in child order), then the remaining integer is split
 *                   among the infinite children with Rosenberg-Strong pairing (as
 *                   IntegerizedStack::split does: pop for all but the last, last gets the remainder).
 *
 *  Every step is a bijection (N = [0,b) + N;  N = [m] x N;  N = prod_i [k_i] x N;  N = N^j), so
 *  decoding and encoding are mutual inverses PROVIDED the decoding recursion always terminates.
 *
 *  TERMINATION. Along any path of the decoding recursion through infinite nonterminals the integer
 *  never increases (subtracting b, dividing by m or by count(child), and Rosenberg-Strong components
 *  are all <= their input). It stays the same only in these cases:
 *     (i)   value v >= 2: only at a nonterminal with b = 0, exactly one infinite rule, all finite
 *           children of count 1, and exactly one infinite child (two or more infinite children give
 *           Rosenberg-Strong components < v). A cycle of such nonterminals has a single useful rule
 *           at each node, each requiring the next node, so none of them has a finite tree --
 *           contradicting productivity. So v strictly decreases infinitely often: impossible forever.
 *     (ii)  v = 1: same argument (m >= 2 or any finite child of count >= 2 send 1 to 0, and
 *           Rosenberg-Strong(1) = (0,1) passes 1 only to the last infinite child).
 *     (iii) v = 0: a nonterminal with b >= 1 decodes 0 to a base tree immediately. For b = 0 the
 *           decoder takes infinite_rules[0] with all children 0. We order infinite_rules so that
 *           infinite_rules[0] is a rule minimizing h(nt) = 1 + max over its infinite children c of
 *           h(c) (with h = 0 when b >= 1); then h strictly decreases along the 0-path.
 *           h is finite for every productive nt (h(nt) <= height of any tree of nt, by induction).
 *  BasicEnumeration fails exactly at (iii): with no terminals, z = 0 can select a self-recursive
 *  rule forever (e.g. W -> f(W) | g(int) decodes 0 as f(f(f(...)))).
 *
 *  The order of trees differs from BasicEnumeration (finite rules join the base block), so integer
 *  codes are not interchangeable between the two classes. Like BasicEnumeration, codes depend on
 *  the grammar's rule order, which depends on the rule probabilities.
 *
 *  The grammar must not be modified after construction (rules are cached by pointer); this is
 *  checked (cheaply, by total rule count) on every public call. After construction all methods
 *  are const and safe to call concurrently from multiple threads.
 */
template<typename Grammar_t>
class GeneralEnumeration {
public:
	using idx_t = enumerationidx_t;
	static constexpr size_t NNT = Grammar_t::N_NTs;

	const Grammar_t* grammar;

protected:

	struct RuleInfo {
		const Rule*         rule = nullptr;
		bool                infinite = false;      // does it have at least one infinite child?
		idx_t               count = 0;             // finite rules: number of trees (product of child counts)
		idx_t               offset = 0;            // finite rules: start of this rule's block within the base block
		std::vector<size_t> finite_children;       // child positions i whose type is a finite nonterminal (child order)
		std::vector<size_t> infinite_children;     // child positions i whose type is an infinite nonterminal (child order)
	};

	struct NTInfo {
		bool                  productive = false;
		bool                  infinite   = false;
		idx_t                 base       = 0;      // finite nt: count(nt); infinite nt: size b of the base block
		std::vector<RuleInfo> finite_rules;        // grammar order
		std::vector<RuleInfo> infinite_rules;      // decoding order (a minimal-height rule first)
	};

	std::array<NTInfo, NNT> info;

	// for toInteger: rule -> (is it an infinite rule?, its index in finite_rules / infinite_rules)
	std::unordered_map<const Rule*, std::pair<bool, size_t>> where;

	size_t total_rules_at_construction = 0;

public:

	explicit GeneralEnumeration(const Grammar_t* g) : grammar(g) {
		analyze();
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Public interface
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	/**
	 * @brief Number of trees of nt: std::nullopt if infinitely many, 0 if nt has no finite tree.
	 */
	[[nodiscard]] std::optional<idx_t> count(const nonterminal_t nt) const {
		check_nt(nt);
		check_unchanged();
		const NTInfo& I = info[nt];
		if(not I.productive) return idx_t{0};
		if(I.infinite)       return std::nullopt;
		return I.base;
	}

	[[nodiscard]] bool is_infinite(const nonterminal_t nt) const {
		check_nt(nt);
		return info[nt].infinite;
	}

	/**
	 * @brief The z'th tree of type nt. Throws EnumerationOutOfRangeException if z >= count(nt).
	 */
	[[nodiscard]] Node toNode(const idx_t z, const nonterminal_t nt) const {
		check_nt(nt);
		check_unchanged();
		const NTInfo& I = info[nt];
		if(not I.productive)
			throw EnumerationOutOfRangeException("*** GeneralEnumeration: nonterminal " + std::to_string(nt) + " has no finite trees");
		if(not I.infinite and z >= I.base)
			throw EnumerationOutOfRangeException("*** GeneralEnumeration: index " + std::to_string(z) + " >= count " +
			                                     std::to_string(I.base) + " of finite nonterminal " + std::to_string(nt));
		return decode(z, nt);
	}

	// Catch the swapped-argument mistake at compile time (BasicEnumeration's version asserts at runtime).
	Node toNode(const nonterminal_t nt, const idx_t z) const = delete;

	/**
	 * @brief Inverse of toNode. Throws std::invalid_argument for incomplete trees or rules this
	 *        grammar cannot use in a complete tree, and EnumerationOverflowException if the code
	 *        does not fit in idx_t.
	 */
	[[nodiscard]] idx_t toInteger(const Node& n) const {
		check_unchanged();
		if(n.is_null() or not n.is_complete())
			throw std::invalid_argument("*** GeneralEnumeration::toInteger requires a complete tree");
		return encode(n);
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Exact, overflow-checked integer helpers (public + static so they can be tested)
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	static idx_t checked_add(const idx_t a, const idx_t b) {
		idx_t r;
		if(__builtin_add_overflow(a, b, &r)) throw EnumerationOverflowException("*** GeneralEnumeration: index overflow (add)");
		return r;
	}

	static idx_t checked_mul(const idx_t a, const idx_t b) {
		idx_t r;
		if(__builtin_mul_overflow(a, b, &r)) throw EnumerationOverflowException("*** GeneralEnumeration: index overflow (multiply)");
		return r;
	}

	/** floor(sqrt(z)), exact for every 64-bit z (the double estimate is corrected with integer arithmetic). */
	static idx_t isqrt(const idx_t z) {
		idx_t m = static_cast<idx_t>(std::sqrt(static_cast<double>(z)));
		while(m > 0 and m > z / m)   --m;  // m*m > z
		while(m + 1 <= z / (m + 1))  ++m;  // (m+1)^2 <= z   (written to avoid overflow)
		return m;
	}

	/** Rosenberg-Strong unpairing (https://arxiv.org/pdf/1706.04129.pdf), exact. */
	static std::pair<idx_t, idx_t> rs_decode(const idx_t z) {
		const idx_t m = isqrt(z);
		const idx_t l = z - m * m;
		if(l < m) return {l, m};
		return {m, m * (m + 2) - z}; // m*(m+2) = (m+1)^2 - 1 <= 2^64 - 1 since m <= 2^32 - 1
	}

	/** Rosenberg-Strong pairing, overflow-checked; the inverse of rs_decode. */
	static idx_t rs_encode(const idx_t x, const idx_t y) {
		const idx_t m = std::max(x, y);
		const idx_t r = checked_mul(m, checked_add(m, 1)); // m(m+1) >= y, so r + x - y below cannot underflow
		return checked_add(r, x) - y;
	}

protected:

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Decoding / encoding
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	Node decode(idx_t z, const nonterminal_t nt) const {
		const NTInfo& I = info[nt];

		if(z < I.base) {
			// base block: find the finite rule whose block contains z (blocks are contiguous, in order)
			for(const RuleInfo& ri : I.finite_rules) {
				if(z < ri.offset + ri.count)
					return decode_rule(ri, z - ri.offset);
			}
			throw std::logic_error("*** GeneralEnumeration: base block inconsistent (bug)");
		}

		// only an infinite nonterminal can get here (toNode range-checks finite ones, and finite
		// children are always given an index < their count)
		if(not I.infinite) throw std::logic_error("*** GeneralEnumeration: finite nonterminal out of range (bug)");

		z -= I.base;
		const idx_t m = I.infinite_rules.size();
		return decode_rule(I.infinite_rules[z % m], z / m);
	}

	Node decode_rule(const RuleInfo& ri, idx_t v) const {
		Node out = grammar->makeNode(ri.rule);

		// finite children: mixed radix, first child is the least significant digit
		for(const size_t i : ri.finite_children) {
			const nonterminal_t t = ri.rule->type(i);
			const idx_t c = info[t].base;
			out.set_child(i, decode(v % c, t));
			v /= c;
		}

		// infinite children: Rosenberg-Strong split of what remains (all but last pop; last takes the rest)
		const size_t k = ri.infinite_children.size();
		for(size_t j = 0; j < k; j++) {
			const size_t i = ri.infinite_children[j];
			idx_t vi = v;
			if(j + 1 < k) {
				const auto [a, rest] = rs_decode(v);
				vi = a;
				v  = rest;
			}
			out.set_child(i, decode(vi, ri.rule->type(i)));
		}

		return out;
	}

	idx_t encode(const Node& n) const {
		const auto it = where.find(n.rule);
		if(it == where.end())
			throw std::invalid_argument("*** GeneralEnumeration::toInteger: rule " + n.rule->format +
			                            " cannot occur in a complete tree of this grammar (or is from another grammar)");

		const NTInfo& I = info[n.rule->nt];
		const auto [is_infinite_rule, pos] = it->second;

		if(not is_infinite_rule) {
			const RuleInfo& ri = I.finite_rules[pos];
			return ri.offset + encode_rule(ri, n); // < base, cannot overflow
		}
		else {
			const RuleInfo& ri = I.infinite_rules[pos];
			const idx_t m = I.infinite_rules.size();
			return checked_add(I.base, checked_add(pos, checked_mul(m, encode_rule(ri, n))));
		}
	}

	idx_t encode_rule(const RuleInfo& ri, const Node& n) const {
		// exact reverse of decode_rule
		idx_t v = 0;

		const size_t k = ri.infinite_children.size();
		if(k > 0) {
			v = encode(n.child(ri.infinite_children[k - 1]));
			for(size_t j = k - 1; j-- > 0; )
				v = rs_encode(encode(n.child(ri.infinite_children[j])), v);
		}

		for(size_t j = ri.finite_children.size(); j-- > 0; ) {
			const size_t i = ri.finite_children[j];
			const idx_t c = info[ri.rule->type(i)].base;
			v = checked_add(encode(n.child(i)), checked_mul(c, v)); // child's code < c, so this is a radix-c digit
		}

		return v;
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Grammar analysis
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	size_t total_rules() const {
		size_t s = 0;
		for(size_t nt = 0; nt < NNT; nt++) s += grammar->rules[nt].size();
		return s;
	}

	void check_unchanged() const {
		if(total_rules() != total_rules_at_construction)
			throw std::logic_error("*** GeneralEnumeration: the grammar changed after the enumerator was constructed; construct a new one");
	}

	static void check_nt(const nonterminal_t nt) {
		if(nt >= NNT) throw std::invalid_argument("*** GeneralEnumeration: nonterminal out of range");
	}

	bool rule_is_useful(const Rule& r, const std::array<bool, NNT>& productive) const {
		for(size_t i = 0; i < r.N; i++)
			if(not productive[r.type(i)]) return false;
		return true;
	}

	void analyze() {
		total_rules_at_construction = total_rules();

		// 1. productivity: least fixed point
		std::array<bool, NNT> productive{};
		for(bool changed = true; changed; ) {
			changed = false;
			for(size_t nt = 0; nt < NNT; nt++) {
				if(productive[nt]) continue;
				for(const Rule& r : grammar->rules[nt]) {
					if(rule_is_useful(r, productive)) { productive[nt] = true; changed = true; break; }
				}
			}
		}

		// 2. reachability (>= 1 step) over useful rules, transitive closure (NNT is tiny)
		std::array<std::array<bool, NNT>, NNT> reach{};
		for(size_t a = 0; a < NNT; a++) {
			if(not productive[a]) continue;
			for(const Rule& r : grammar->rules[a])
				if(rule_is_useful(r, productive))
					for(size_t i = 0; i < r.N; i++) reach[a][r.type(i)] = true;
		}
		for(size_t k = 0; k < NNT; k++)
			for(size_t a = 0; a < NNT; a++)
				if(reach[a][k])
					for(size_t b = 0; b < NNT; b++)
						if(reach[k][b]) reach[a][b] = true;

		// 3. infinite = productive and reaches (or is) a nonterminal on a cycle
		for(size_t a = 0; a < NNT; a++) {
			info[a].productive = productive[a];
			bool inf = false;
			if(productive[a]) {
				for(size_t b = 0; b < NNT and not inf; b++)
					inf = (a == b or reach[a][b]) and reach[b][b];
			}
			info[a].infinite = inf;
		}

		// 4. exact counts for finite productive nonterminals (their useful-rule graph is a DAG)
		std::array<int, NNT> state{}; // 0 = not visited, 1 = in progress, 2 = done
		for(size_t a = 0; a < NNT; a++)
			if(productive[a] and not info[a].infinite) compute_finite_count(a, state, productive);

		// 5. classify the useful rules of every productive nonterminal
		for(size_t a = 0; a < NNT; a++) {
			NTInfo& I = info[a];
			if(not I.productive) continue;

			I.finite_rules.clear();
			I.infinite_rules.clear();
			idx_t offset = 0;
			for(const Rule& r : grammar->rules[a]) {
				if(not rule_is_useful(r, productive)) continue; // can never occur in a complete tree

				RuleInfo ri;
				ri.rule = &r;
				for(size_t i = 0; i < r.N; i++) {
					if(info[r.type(i)].infinite) ri.infinite_children.push_back(i);
					else                         ri.finite_children.push_back(i);
				}
				ri.infinite = not ri.infinite_children.empty();

				if(ri.infinite) {
					I.infinite_rules.push_back(std::move(ri));
				}
				else {
					idx_t c = 1;
					for(const size_t i : ri.finite_children) c = checked_mul_named(c, info[r.type(i)].base, a);
					ri.count  = c;
					ri.offset = offset;
					offset = checked_add_named(offset, c, a);
					I.finite_rules.push_back(std::move(ri));
				}
			}

			if(I.infinite) {
				I.base = offset;
				if(I.infinite_rules.empty())
					throw std::logic_error("*** GeneralEnumeration: infinite nonterminal without an infinite rule (bug)");
			}
			else if(I.base != offset) {
				throw std::logic_error("*** GeneralEnumeration: finite count mismatch (bug)");
			}
		}

		// 6. minimal heights h and a minimal-height first rule for base-less infinite nonterminals
		constexpr size_t INF = std::numeric_limits<size_t>::max();
		std::array<size_t, NNT> h;
		std::array<size_t, NNT> best_rule;
		h.fill(INF);
		best_rule.fill(0);
		for(size_t a = 0; a < NNT; a++)
			if(info[a].infinite and info[a].base > 0) h[a] = 0;

		for(bool changed = true; changed; ) {
			changed = false;
			for(size_t a = 0; a < NNT; a++) {
				if(not info[a].infinite or info[a].base > 0) continue;
				const auto& rules = info[a].infinite_rules;
				for(size_t k = 0; k < rules.size(); k++) {
					size_t hr = 0;
					for(const size_t i : rules[k].infinite_children) {
						const size_t hc = h[rules[k].rule->type(i)];
						hr = (hc == INF ? INF : std::max(hr, hc + 1));
					}
					if(hr < h[a]) { h[a] = hr; best_rule[a] = k; changed = true; }
				}
			}
		}

		for(size_t a = 0; a < NNT; a++) {
			NTInfo& I = info[a];
			if(not I.infinite) continue;
			if(h[a] == INF)
				throw std::logic_error("*** GeneralEnumeration: productive nonterminal with no finite height (bug)");
			if(I.base == 0 and best_rule[a] != 0) {
				// move the minimal-height rule to the front, keeping the others in grammar order
				RuleInfo first = std::move(I.infinite_rules[best_rule[a]]);
				I.infinite_rules.erase(I.infinite_rules.begin() + best_rule[a]);
				I.infinite_rules.insert(I.infinite_rules.begin(), std::move(first));
			}
		}

		// 7. lookup table for toInteger
		where.clear();
		for(size_t a = 0; a < NNT; a++) {
			for(size_t k = 0; k < info[a].finite_rules.size(); k++)   where[info[a].finite_rules[k].rule]   = {false, k};
			for(size_t k = 0; k < info[a].infinite_rules.size(); k++) where[info[a].infinite_rules[k].rule] = {true, k};
		}
	}

	idx_t compute_finite_count(const size_t a, std::array<int, NNT>& state, const std::array<bool, NNT>& productive) {
		if(state[a] == 2) return info[a].base;
		if(state[a] == 1) throw std::logic_error("*** GeneralEnumeration: cycle below a finite nonterminal (bug)");
		state[a] = 1;

		idx_t total = 0;
		for(const Rule& r : grammar->rules[a]) {
			if(not rule_is_useful(r, productive)) continue;
			idx_t c = 1;
			for(size_t i = 0; i < r.N; i++)
				c = checked_mul_named(c, compute_finite_count(r.type(i), state, productive), a);
			total = checked_add_named(total, c, a);
		}

		info[a].base = total;
		state[a] = 2;
		return total;
	}

	static idx_t checked_add_named(const idx_t x, const idx_t y, const size_t nt) {
		idx_t r;
		if(__builtin_add_overflow(x, y, &r))
			throw EnumerationOverflowException("*** GeneralEnumeration: the number of finite trees of nonterminal " + std::to_string(nt) + " does not fit in 64 bits");
		return r;
	}

	static idx_t checked_mul_named(const idx_t x, const idx_t y, const size_t nt) {
		idx_t r;
		if(__builtin_mul_overflow(x, y, &r))
			throw EnumerationOverflowException("*** GeneralEnumeration: the number of finite trees of nonterminal " + std::to_string(nt) + " does not fit in 64 bits");
		return r;
	}
};
