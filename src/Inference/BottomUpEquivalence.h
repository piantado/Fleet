#pragma once

/**
 * @file BottomUpEquivalence.h
 * @brief Bottom-up enumeration with observational-equivalence pruning, designed to keep memory small.
 *
 * WHAT IT DOES
 *   Programs are built bottom-up in order of increasing integer cost. Every candidate is evaluated on a fixed
 *   set of inputs (e.g. ARC train + test inputs). Two programs of the same type that produce identical values
 *   (including identical error patterns) on every input are interchangeable as subterms of any larger
 *   program, so only one representative -- the lowest-cost one -- is kept per equivalence class.
 *   Parents are only ever built from class representatives, which collapses the search space.
 *
 * COST / ORDER
 *   Each rule r with P(r|nt) = p_r / Z_nt gets integer cost  ic(r) = max(1, round(-log P(r|nt) / cost_quantum)).
 *   A program's cost is the sum of ic over its nodes, i.e. a quantized version of -log prior. Levels are
 *   enumerated exactly in increasing cost. Within a level, the representative kept is the one with the
 *   highest exact log prior. Across levels, the order is exact in quantized cost, so it can differ from
 *   exact prior order by at most (#nodes * cost_quantum / 2) nats. Use a smaller quantum for finer order
 *   (at the price of more, emptier levels).
 *
 * MEMORY LAYOUT (per equivalence class, independent of the value type)
 *   - Class record: 16 bytes (rule id, cache slot, float log prior, offset of children)
 *   - Children:      4 bytes per child (indices into the child type's class array), in a chunked arena
 *   - Fingerprint:  16 bytes per slot in an open-addressing table (load factor 0.375..0.75 => ~21-43 bytes)
 *   => roughly 45-70 bytes/class, plus a 24-byte slot for each class whose values are currently cached
 *   (measured: ~100 bytes/class in total when everything is cached). Values (often the dominant cost,
 *   e.g. ARC grids) are NOT stored per class. They live in a byte-budgeted cache with CLOCK eviction
 *   (value_budget_bytes; per-block overhead is ~90 bytes, so for tiny types like int the budget mostly
 *   buys overhead). An evicted value is recomputed
 *   from the class's representative (rule + child classes) on demand. Recomputation is exact because
 *   the primitives must be deterministic (see REQUIREMENTS). Classes and children are stored in
 *   std::deque, so growth never needs a 2x reallocation copy.
 *
 * SOUNDNESS
 *   - Equivalence is decided by a 96-bit fingerprint of the value vector. A false merge (two different value
 *     vectors with the same fingerprint) can only make the search *incomplete* (it can never yield a wrong
 *     answer, because reported solutions are compared exactly to the targets and, by default, re-run through
 *     Fleet's own VM). The chance of a false merge among n classes is about n^2 / 2^97 (~6e-12 for n=1e9),
 *     assuming the hash behaves like a random function. Set verify_equivalence=true to make merging exact:
 *     each fingerprint hit is then confirmed with operator== on the values (this may recompute evicted values).
 *   - Errors: a primitive that throws VMSRuntimeError makes that input's value an error. Errors propagate
 *     through ordinary rules exactly as in Fleet's VM (the whole program fails). If/And/Or are evaluated
 *     lazily with Fleet's short-circuit semantics (an error in an unused branch is ignored).
 *   - Error pruning: if the grammar has no If/And/Or rules, any program that errors on an input with a target
 *     can never be part of a solution (errors are absorbing), so such classes are dropped. This is exact and
 *     only enabled when it is sound.
 *
 * REQUIREMENTS (checked where possible, otherwise documented)
 *   - Deterministic, side-effect-free primitives (no Flip/Sample/recursion/memoization/lexicon ops; these are
 *     rejected at construction).
 *   - Every grammar type T must be default-constructible and copyable, and must have BUE::Feed<T>. A default
 *     is provided for arithmetic/enum/pointer types, std::pair/tuple/array and standard containers. For your
 *     own types (e.g. Grid), specialize BUE::Feed<T>::apply. It MUST feed everything that can affect a
 *     primitive's behavior (e.g. a Grid's width, height and pixels, plus any other field the ops read). If
 *     two behaviorally different values feed identical streams, they will be merged (unsound). For
 *     function-valued types (std::function), any Feed (e.g. applying to probe inputs) is a heuristic: two
 *     functions that agree on probes but differ elsewhere would be merged.
 *   - Optionally specialize BUE::Bytes<T>::of for accurate cache accounting of heap-owning types.
 *   - At most 64 inputs (error patterns are stored as a 64-bit mask).
 *   - The grammar must not be modified while an instance exists (rules are referenced by pointer).
 *
 * PERFORMANCE NOTES (measured with rdtsc phase timers on int and 10x10-grid benchmarks)
 *   - ~70% of time is evaluation: the primitives themselves plus one copy of each argument (Fleet primitives take
 *     arguments by value, and cached values must survive). Hashing ~16%, table lookup ~6%, cache ~5%.
 *   - Primitives that fail by throwing VMSRuntimeError are expensive (C++ unwinding is ~1-2 us per throw); on an
 *     int grammar with division this was ~40% of all instructions. Only a non-throwing error convention in Fleet
 *     would avoid it.
 *   - Feed<T> cost scales with bytes fed: a Grid stored as uint8_t (or fed packed) hashes ~4x faster than int cells
 *     and its values use ~4x less cache.
 *   - Implemented: scratch evaluation (no allocation for duplicates), VM stacks cleared rather than rebuilt after
 *     errors, evaluation stops at the first error on a target input when that candidate will be pruned, and a hasher
 *     with independent lanes.
 *
 * TYPICAL USE (ARC): inputs = train inputs + test inputs; targets = train outputs, std::nullopt for test.
 *   Merging over train+test inputs keeps programs apart that agree on train but predict differently on test,
 *   so the callback sees each distinct test prediction once, in order of cost, with its max-prior program.
 *
 *		BottomUpEquivalence<MyGrammar> bue(&grammar, inputs);
 *		bue.set_targets(targets);
 *		bue.run(max_level, [&](const Node& prog, double lp, uint32_t level,
 *		                       const std::vector<output_t>& outs, uint64_t errmask) {
 *			print(level, lp, prog);   // outs[i] for test i is this program's prediction
 *			return true;              // false = stop
 *		});
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "VMSRuntimeError.h"
#include "VMStatus.h"

namespace BUE {

	/// 96-bit fingerprint of a value vector
	struct FP {
		uint64_t a;
		uint32_t b;
		bool operator==(const FP&) const = default;
	};

	/**
	 * @brief Streaming hasher with 8 independent 64-bit lanes; word j is absorbed into lane j%8 by
	 *        s <- rotl((s ^ w) * K, 31), a bijection of s for fixed w (and of w for fixed s). The final fold runs two
	 *        chains x and y over the lanes, each step a bijection of the chain value, so two streams of equal length
	 *        that differ in a single word ALWAYS get different x (no collision is possible for single-word changes).
	 *        Otherwise collisions behave like a random 96-bit function. The independent lanes give instruction-level
	 *        parallelism (one multiply per word, ~8 in flight). Not cryptographic; for non-adversarial data.
	 */
	class Hasher {
		static constexpr uint64_t K  = 0x9E3779B97F4A7C15ull, K2 = 0xC2B2AE3D27D4EB4Full,
		                          K3 = 0x165667B19E3779F9ull, K4 = 0xD6E8FEB86659FD93ull;
		uint64_t s_[8] = {0x243F6A8885A308D3ull, 0x13198A2E03707344ull, 0xA4093822299F31D0ull, 0x082EFA98EC4E6C89ull,
		                  0x452821E638D01377ull, 0xBE5466CF34E90C6Cull, 0xC0AC29B7C97C50DDull, 0x3F84D5B5B5470917ull};
		uint64_t n_ = 0; // number of words absorbed

		static constexpr uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
		static constexpr uint64_t fmix(uint64_t x) {
			x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
			x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
			x ^= x >> 33; return x;
		}
		static constexpr uint64_t upd(uint64_t s, uint64_t w) { return rotl((s ^ w) * K, 31); }
	public:
		void word(uint64_t w) {
			uint64_t& s = s_[n_ & 7];
			s = upd(s, w);
			++n_;
		}
		void bytes(const void* p, size_t len) {
			const unsigned char* c = static_cast<const unsigned char*>(p);
			size_t i = 0;
			// align to lane 0 so the unrolled loop assigns words to the same lanes as word() would
			for(; (n_ & 7) != 0 and i + 8 <= len; i += 8) { uint64_t w; std::memcpy(&w, c + i, 8); word(w); }
			if(i + 64 <= len) {
				uint64_t t[8]; std::memcpy(t, s_, sizeof t);
				for(; i + 64 <= len; i += 64) {
					uint64_t w[8]; std::memcpy(w, c + i, 64);
					for(size_t j = 0; j < 8; j++) t[j] = upd(t[j], w[j]);
					n_ += 8;
				}
				std::memcpy(s_, t, sizeof t);
			}
			for(; i + 8 <= len; i += 8) { uint64_t w; std::memcpy(&w, c + i, 8); word(w); }
			if(i < len) { // tail: <= 7 bytes in the low 56 bits, tail length in the top byte
				uint64_t w = 0; std::memcpy(&w, c + i, len - i);
				word(w | (uint64_t(len - i) << 56));
			}
		}
		FP finish() const {
			uint64_t x = fmix(n_ * K4 + K);   // n_ separates streams of different lengths
			uint64_t y = fmix(n_ * K2 + K3);
			for(size_t j = 0; j < 8; j++) {
				x = fmix(x ^ s_[j]) + K2;        // bijective in x for fixed lane, and in the lane for fixed x
				y = fmix(y + rotl(s_[j], 29)) ^ K3;
			}
			y = fmix(y ^ x);
			return FP{ x, uint32_t(y ^ (y >> 32)) };
		}
	};

	template<typename> inline constexpr bool always_false = false;
	template<typename T, template<typename...> class Tmpl> struct is_spec : std::false_type {};
	template<template<typename...> class Tmpl, typename... A> struct is_spec<Tmpl<A...>, Tmpl> : std::true_type {};
	template<typename T> struct is_std_array : std::false_type {};
	template<typename V, size_t N> struct is_std_array<std::array<V,N>> : std::true_type {};

	/**
	 * @brief Feed a value into a Hasher. Specialize for your own types. Contract: if two values feed the same
	 *        sequence of words, they must be behaviorally identical to every primitive in the grammar.
	 */
	template<typename T>
	struct Feed {
		static void apply(Hasher& h, const T& x) {
			if constexpr (std::is_arithmetic_v<T> or std::is_enum_v<T> or std::is_pointer_v<T>) {
				h.bytes(&x, sizeof(T)); // bitwise identity => identical behavior (e.g. 0.0 vs -0.0 stay distinct)
			}
			else if constexpr (is_spec<T, std::pair>::value) {
				Feed<std::remove_cv_t<typename T::first_type>>::apply(h, x.first);
				Feed<std::remove_cv_t<typename T::second_type>>::apply(h, x.second);
			}
			else if constexpr (is_spec<T, std::tuple>::value) {
				std::apply([&](const auto&... e) { (Feed<std::remove_cvref_t<decltype(e)>>::apply(h, e), ...); }, x);
			}
			else if constexpr (std::is_same_v<T, std::vector<bool>>) {
				h.word(x.size());
				for(bool e : x) h.word(e);
			}
			else if constexpr (requires { typename T::value_type; x.size(); x.begin(); x.end(); }) {
				using V = std::remove_cv_t<typename T::value_type>;
				h.word(x.size());
				if constexpr ((std::is_arithmetic_v<V> or std::is_enum_v<V>) and
				              requires { { x.data() } -> std::convertible_to<const V*>; }) {
					h.bytes(x.data(), x.size() * sizeof(V)); // contiguous fast path (vector, string, array)
				}
				else {
					for(const auto& e : x) Feed<V>::apply(h, e); // ordered iteration; unordered containers may
					                                             // fingerprint equal sets differently (no false merge)
				}
			}
			else {
				static_assert(always_false<T>, "*** BottomUpEquivalence: specialize BUE::Feed<T> for this grammar type "
				                               "(feed every field that can affect a primitive's behavior).");
			}
		}
	};

	/// Approximate heap+inline bytes of a value, for cache accounting. Specialize for heap-owning types.
	template<typename T>
	struct Bytes {
		static size_t of(const T& x) {
			if constexpr (std::is_same_v<T, std::vector<bool>>) {
				return sizeof(T) + x.capacity() / 8;
			}
			else if constexpr (requires { typename T::value_type; x.capacity(); x.begin(); }) {
				using V = std::remove_cv_t<typename T::value_type>;
				size_t s = sizeof(T) + x.capacity() * sizeof(V);
				if constexpr (not std::is_trivially_copyable_v<V>)
					for(const auto& e : x) s += Bytes<V>::of(e) - sizeof(V);
				return s;
			}
			else {
				return sizeof(T);
			}
		}
	};

	struct BlockBase {
		uint64_t err   = 0; // bit i set <=> evaluation on input i raised an error
		size_t   bytes = 0; // approximate memory footprint, for the cache budget
		virtual ~BlockBase() = default;
	};

	/// The values of one program on all inputs. Entries at error positions are default-constructed and ignored.
	template<typename T>
	struct Block final : BlockBase {
		std::vector<T> v;
	};

	using BlockPtr = std::shared_ptr<const BlockBase>;

	template<typename X, typename Tuple> struct tuple_has;
	template<typename X, typename... Ts> struct tuple_has<X, std::tuple<Ts...>> : std::bool_constant<(std::is_same_v<X,Ts> or ...)> {};

	/// Open-addressing set of fingerprints -> class index. 16 bytes per slot; duplicates of a fingerprint are
	/// allowed (needed when exact verification finds a genuine collision).
	class FPTable {
	public:
		static constexpr uint32_t EMPTY = std::numeric_limits<uint32_t>::max();
	private:
		struct Slot { uint64_t a; uint32_t b; uint32_t id; };
		static_assert(sizeof(Slot) == 16);
		std::vector<Slot> t_;
		size_t n_ = 0;

		void place(const Slot& s) {
			const size_t mask = t_.size() - 1;
			size_t i = size_t(s.a) & mask;
			while(t_[i].id != EMPTY) i = (i + 1) & mask;
			t_[i] = s;
		}
		void grow() {
			std::vector<Slot> old(t_.size() * 2, Slot{0, 0, EMPTY});
			old.swap(t_);
			for(const Slot& s : old) if(s.id != EMPTY) place(s);
		}
	public:
		FPTable() : t_(16, Slot{0, 0, EMPTY}) {}

		/// Returns the id of a stored entry with this fingerprint for which same(id) is true, or EMPTY.
		template<typename Same>
		uint32_t find(const FP& fp, Same&& same) const {
			const size_t mask = t_.size() - 1;
			for(size_t i = size_t(fp.a) & mask; ; i = (i + 1) & mask) {
				const Slot& s = t_[i];
				if(s.id == EMPTY) return EMPTY;
				if(s.a == fp.a and s.b == fp.b and same(s.id)) return s.id;
			}
		}
		void insert(const FP& fp, uint32_t id) {
			assert(id != EMPTY);
			if((n_ + 1) * 4 > t_.size() * 3) grow(); // max load 0.75
			place(Slot{fp.a, fp.b, id});
			++n_;
		}
		size_t size()  const { return n_; }
		size_t bytes() const { return t_.capacity() * sizeof(Slot); }
	};

} // namespace BUE


template<typename Grammar_t>
class BottomUpEquivalence {
public:
	using input_t   = typename Grammar_t::input_t;
	using output_t  = typename Grammar_t::output_t;
	using VMS_t     = typename Grammar_t::VirtualMachineState_t;
	using FT        = typename Grammar_t::FT;
	using TypeTuple = typename Grammar_t::TypeTuple;
	using BlockPtr  = BUE::BlockPtr;

	static constexpr size_t   N_NTs        = Grammar_t::N_NTs;
	static constexpr uint32_t NONE         = std::numeric_limits<uint32_t>::max();
	static constexpr size_t   MAX_EXAMPLES = 64;
	static constexpr size_t   MAX_ARITY    = 16;

	struct Params {
		double   cost_quantum        = 0.5;            // nats of -log prior per integer cost unit
		size_t   value_budget_bytes  = size_t(1) << 30; // cache budget for value vectors
		uint64_t max_classes         = 200'000'000;     // total classes (all types); stop when reached
		bool     verify_equivalence  = false;           // confirm fingerprint hits with operator== (exact merging)
		bool     prune_errors        = true;            // drop classes erroring on a target input (only when sound)
		bool     check_solutions     = true;            // re-run each reported program through Fleet's VM
		bool     allow_custom_ops    = false;           // accept Op::Custom1..5 rules as ordinary stack primitives
	};

	struct Stats {
		uint64_t candidates = 0, classes = 0, duplicates = 0, replaced = 0, pruned_error = 0;
		uint64_t cache_hits = 0, recomputes = 0, evictions = 0, collisions = 0;
		size_t   cache_bytes = 0;
	};

	/// Called for each reported class of the output type. Return false to stop.
	using Callback = std::function<bool(const Node& program, double logprior, uint32_t level,
	                                    const std::vector<output_t>& outputs, uint64_t errmask)>;

private:
	struct Class {
		uint32_t rule; // index into rules_
		uint32_t slot; // cache slot or NONE
		float    lp;   // exact log prior of the representative (sum over its nodes)
		uint32_t kids; // offset into arena_ of the child class indices
	};
	static_assert(sizeof(Class) == 16);

	struct Slot {
		BlockPtr      blk;
		nonterminal_t nt  = 0;
		uint32_t      cls = NONE;
		bool          ref = false; // CLOCK reference bit
	};

	using Kids    = const BUE::BlockBase* const*;
	using MutBlock = std::shared_ptr<BUE::BlockBase>;

	struct NTOps {
		// evaluate rule r into out (which must hold n values); returns false if it stopped early because an
		// input in abort_mask errored (out is then incomplete and must be discarded)
		bool      (*eval)(BottomUpEquivalence*, const Rule*, Kids, BUE::BlockBase& out, uint64_t abort_mask);
		void      (*push)(VMS_t&, const BUE::BlockBase&, size_t);
		BUE::FP   (*fingerprint)(const BUE::BlockBase&);
		bool      (*equal)(const BUE::BlockBase&, const BUE::BlockBase&);
		MutBlock  (*make)(size_t n);                       // new block of n default values
		MutBlock  (*take)(BUE::BlockBase& scratch, size_t n); // move scratch's values into a new block; refill scratch
		void      (*set_bytes)(BUE::BlockBase&);
	};

	const Grammar_t*                          grammar_;
	const std::vector<input_t>                inputs_;
	const Params                              params_;
	const output_t                            err_output_{};   // must precede vms_ (VMS keeps a reference)
	std::vector<std::unique_ptr<VMS_t>>       vms_;           // one reusable VM state per input

	std::vector<const Rule*>                  rules_;
	std::vector<uint32_t>                     icost_;
	std::vector<float>                        rlp_;
	bool                                      has_lazy_ = false;

	std::array<std::deque<Class>, N_NTs>      classes_;
	std::array<std::vector<uint32_t>, N_NTs>  level_begin_;   // level_begin_[nt][c] = first class index of level c
	std::array<BUE::FPTable, N_NTs>           tables_;
	std::deque<uint32_t>                      arena_;

	std::vector<Slot>                         slots_;
	std::vector<uint32_t>                     free_slots_;
	size_t                                    hand_ = 0;

	std::vector<std::optional<output_t>>      targets_;
	uint64_t                                  target_mask_ = 0;
	uint64_t                                  prune_mask_  = 0;

	uint32_t                                  level_ = 0;
	bool                                      stopped_ = false;
	bool                                      level_complete_ = true;
	std::vector<uint32_t>                     matches_;

	// scratch for the current candidate (product() recursion)
	std::array<uint32_t, MAX_ARITY>           lv_{};
	std::array<uint32_t, MAX_ARITY>           kid_{};
	std::array<BlockPtr, MAX_ARITY>           kb_{};   // keeps the current children alive
	std::array<const BUE::BlockBase*, MAX_ARITY> kp_{}; // raw view of kb_ for evaluation
	std::array<MutBlock, N_NTs>               scratch_; // candidate values are computed here; copied out only if new

	Stats                                     stats_;

	static constexpr nonterminal_t out_nt() { return Grammar_t::template nt<output_t>(); }
	uint64_t bit(size_t i) const { return uint64_t(1) << i; }

	template<size_t I> using T_at = std::tuple_element_t<I, TypeTuple>;

	template<typename T>
	static const BUE::Block<T>& as(const BUE::BlockBase& b) { return static_cast<const BUE::Block<T>&>(b); }

	static bool is_lazy(Op o) { return o == Op::If or o == Op::And or o == Op::Or; }
	bool is_direct(Op o) const {
		if(o == Op::Standard or o == Op::X or o == Op::Not or o == Op::Implies or o == Op::Iff) return true;
		if(params_.allow_custom_ops and (o == Op::Custom1 or o == Op::Custom2 or o == Op::Custom3 or
		                                 o == Op::Custom4 or o == Op::Custom5)) return true;
		return false;
	}

	void reset_vms(size_t i) {
		vms_[i] = std::make_unique<VMS_t>(inputs_[i], err_output_, nullptr);
	}

	template<size_t... I>
	static void clear_stacks(VMS_t& vm, std::index_sequence<I...>) { (vm.template stack<T_at<I>>().clear(), ...); }

	/// Restore VM state i after a primitive failed (it may have left arguments or partial results on the stacks).
	/// Clearing keeps the stacks' capacity, which is much cheaper than rebuilding the state. Supported ops never
	/// touch the program, x-stack or memoization tables; if they look disturbed anyway, rebuild from scratch.
	void clean_vms(size_t i) {
		VMS_t& vm = *vms_[i];
		if(vm.xstack.size() != 1 or not vm.program.empty()) { reset_vms(i); return; }
		clear_stacks(vm, std::make_index_sequence<N_NTs>{});
		vm.status = vmstatus_t::GOOD;
	}

	// ---------------------------------------------------------------- per-type operations
	template<size_t I>
	static bool eval_rule(BottomUpEquivalence* self, const Rule* r, Kids kids, BUE::BlockBase& outb, uint64_t abort_mask) {
		using T = T_at<I>;
		static_assert(std::is_default_constructible_v<T> and std::is_copy_constructible_v<T>,
		              "*** BottomUpEquivalence: grammar types must be default-constructible and copyable");
		const size_t n = self->inputs_.size();
		auto* out = &static_cast<BUE::Block<T>&>(outb);
		assert(out->v.size() == n);
		uint64_t err = 0;

		if(r->is_a(Op::If)) {
			// Fleet: evaluate the condition; then only the chosen branch
			if constexpr (BUE::tuple_has<bool, TypeTuple>::value) {
				const auto& c = as<bool>(*kids[0]);
				const auto& x = as<T>(*kids[1]);
				const auto& y = as<T>(*kids[2]);
				for(size_t i = 0; i < n; i++) {
					const uint64_t b = self->bit(i);
					if(c.err & b) { err |= b; continue; }
					const auto& src = c.v[i] ? x : y;
					if(src.err & b) { err |= b; continue; }
					out->v[i] = src.v[i];
				}
			}
			else throw std::logic_error("*** BottomUpEquivalence: If rule in a grammar without bool");
		}
		else if(r->is_a(Op::And) or r->is_a(Op::Or)) {
			// Fleet: evaluate child 0; if it decides the result, child 1 is skipped
			if constexpr (std::is_same_v<T, bool>) {
				const bool is_and = r->is_a(Op::And);
				const auto& a = as<bool>(*kids[0]);
				const auto& c = as<bool>(*kids[1]);
				for(size_t i = 0; i < n; i++) {
					const uint64_t b = self->bit(i);
					if(a.err & b) { err |= b; continue; }
					if(a.v[i] != is_and) { out->v[i] = not is_and; continue; } // and: false; or: true
					if(c.err & b) { err |= b; continue; }
					out->v[i] = c.v[i];
				}
			}
			else throw std::logic_error("*** BottomUpEquivalence: And/Or rule must return bool");
		}
		else {
			// strict: any child error is an error (Fleet aborts the whole program)
			uint64_t kerr = 0;
			for(size_t k = 0; k < r->N; k++) kerr |= kids[k]->err;
			if(kerr & abort_mask) { out->err = kerr; return false; }
			auto* f = reinterpret_cast<FT*>(r->fptr);
			for(size_t i = 0; i < n; i++) {
				const uint64_t b = self->bit(i);
				if(kerr & b) { err |= b; continue; }
				VMS_t& vm = *self->vms_[i];
				// push children in order 0..N-1, exactly as the linearized program leaves them on the stacks
				for(size_t k = 0; k < r->N; k++) ops_[r->child_types[k]].push(vm, *kids[k], i);
				bool ok = true;
				try { (*f)(&vm, r->arg); }
				catch(VMSRuntimeError&) { ok = false; }
				if(ok and vm.status != vmstatus_t::GOOD) {
					if(vm.status != vmstatus_t::ERROR)
						throw std::logic_error("*** BottomUpEquivalence: rule '" + r->format + "' set a non-error VM status (stochastic op?)");
					ok = false;
				}
				if(not ok) {
					err |= b;
					self->clean_vms(i); // stacks may hold leftovers
					if(b & abort_mask) { out->err = err; return false; }
					continue;
				}
				if(not vm.template exactly_one<T>())
					throw std::logic_error("*** BottomUpEquivalence: rule '" + r->format + "' did not leave exactly one value of its type");
				out->v[i] = vm.template getpop<T>();
			}
		}

		out->err = err;
		return (err & abort_mask) == 0;
	}

	template<size_t I>
	static void set_bytes_block(BUE::BlockBase& bb) {
		using T = T_at<I>;
		auto& b = static_cast<BUE::Block<T>&>(bb);
		size_t bytes = sizeof(BUE::Block<T>) + 16 /* shared_ptr control block */ + (b.v.capacity() - b.v.size()) * sizeof(T);
		for(const T& x : b.v) bytes += BUE::Bytes<T>::of(x);
		b.bytes = bytes;
	}

	template<size_t I>
	static MutBlock make_block(size_t n) {
		auto b = std::make_shared<BUE::Block<T_at<I>>>();
		b->v.resize(n);
		return b;
	}

	template<size_t I>
	static MutBlock take_block(BUE::BlockBase& sb, size_t n) {
		using T = T_at<I>;
		auto& s = static_cast<BUE::Block<T>&>(sb);
		auto b = std::make_shared<BUE::Block<T>>();
		b->err = s.err;
		b->v   = std::move(s.v);
		s.v.clear();   // a moved-from vector is valid but unspecified
		s.v.resize(n);
		set_bytes_block<I>(*b);
		return b;
	}

	template<size_t I>
	static void push_value(VMS_t& vm, const BUE::BlockBase& b, size_t i) {
		using T = T_at<I>;
		vm.template push<T>(T(as<T>(b).v[i]));
	}

	template<size_t I>
	static BUE::FP fingerprint_block(const BUE::BlockBase& bb) {
		using T = T_at<I>;
		const auto& b = as<T>(bb);
		BUE::Hasher h;
		h.word(b.err);
		for(size_t i = 0; i < b.v.size(); i++)
			if(not ((b.err >> i) & 1)) BUE::Feed<T>::apply(h, b.v[i]);
		return h.finish();
	}

	template<size_t I>
	static bool equal_blocks(const BUE::BlockBase& xb, const BUE::BlockBase& yb) {
		using T = T_at<I>;
		const auto& x = as<T>(xb);
		const auto& y = as<T>(yb);
		if(x.err != y.err) return false;
		if constexpr (std::equality_comparable<T>) {
			for(size_t i = 0; i < x.v.size(); i++)
				if(not ((x.err >> i) & 1) and not (x.v[i] == y.v[i])) return false;
			return true;
		}
		else {
			return fingerprint_block<I>(xb) == fingerprint_block<I>(yb); // no operator==: fingerprint only
		}
	}

	template<size_t... I>
	static std::array<NTOps, N_NTs> make_ops(std::index_sequence<I...>) {
		return {{ NTOps{ &eval_rule<I>, &push_value<I>, &fingerprint_block<I>, &equal_blocks<I>,
		                 &make_block<I>, &take_block<I>, &set_bytes_block<I> }... }};
	}
	static inline const std::array<NTOps, N_NTs> ops_ = make_ops(std::make_index_sequence<N_NTs>{});

	// ---------------------------------------------------------------- value cache (CLOCK)
	void evict_one() {
		for(;;) {
			if(hand_ >= slots_.size()) hand_ = 0;
			Slot& s = slots_[hand_];
			const uint32_t si = uint32_t(hand_++);
			if(not s.blk) continue;
			if(s.ref) { s.ref = false; continue; }
			classes_[s.nt][s.cls].slot = NONE;
			stats_.cache_bytes -= s.blk->bytes;
			s.blk.reset(); // outstanding users keep their shared_ptr alive
			free_slots_.push_back(si);
			++stats_.evictions;
			return;
		}
	}

	void cache_insert(nonterminal_t nt, uint32_t idx, const BlockPtr& b) {
		if(b->bytes > params_.value_budget_bytes) return; // never cacheable; will be recomputed when needed
		while(stats_.cache_bytes + b->bytes > params_.value_budget_bytes) evict_one(); // terminates: cache_bytes>0
		uint32_t si;
		if(not free_slots_.empty()) { si = free_slots_.back(); free_slots_.pop_back(); }
		else { si = uint32_t(slots_.size()); slots_.emplace_back(); }
		slots_[si] = Slot{b, nt, idx, true};
		classes_[nt][idx].slot = si;
		stats_.cache_bytes += b->bytes;
	}

	/// Values of a class: from the cache, or recomputed exactly from its representative.
	BlockPtr get_block(nonterminal_t nt, uint32_t idx) {
		const Class& c = classes_[nt][idx];
		if(c.slot != NONE) {
			slots_[c.slot].ref = true;
			++stats_.cache_hits;
			return slots_[c.slot].blk;
		}
		++stats_.recomputes;
		const Rule* r = rules_[c.rule];
		const uint32_t kidoff = c.kids;
		std::array<BlockPtr, MAX_ARITY> kids;            // keeps children alive during evaluation
		std::array<const BUE::BlockBase*, MAX_ARITY> kp{};
		for(size_t k = 0; k < r->N; k++) {
			kids[k] = get_block(r->child_types[k], arena_[kidoff + k]);
			kp[k]   = kids[k].get();
		}
		MutBlock b = ops_[nt].make(inputs_.size());
		[[maybe_unused]] const bool complete = ops_[nt].eval(this, r, kp.data(), *b, 0);
		assert(complete);
		ops_[nt].set_bytes(*b);
		cache_insert(nt, idx, b);
		return b;
	}

	// ---------------------------------------------------------------- enumeration
	std::pair<uint32_t,uint32_t> level_range(nonterminal_t nt, uint32_t l) const {
		assert(l < level_);
		return { level_begin_[nt][l], level_begin_[nt][l + 1] };
	}

	uint32_t push_kids(size_t N) {
		if(N == 0) return 0;
		if(arena_.size() + N > NONE) throw std::length_error("*** BottomUpEquivalence: child arena overflow");
		const uint32_t off = uint32_t(arena_.size());
		for(size_t k = 0; k < N; k++) arena_.push_back(kid_[k]);
		return off;
	}

	bool matches_target(const BUE::BlockBase& bb) const {
		if(target_mask_ == 0) return true; // no targets: report every new output-type class
		const auto& b = as<output_t>(bb);
		for(size_t i = 0; i < targets_.size(); i++) {
			if(not targets_[i].has_value()) continue;
			if((b.err >> i) & 1) return false;
			if(not (b.v[i] == *targets_[i])) return false;
		}
		return true;
	}

	void try_candidate(uint32_t ri, float lp) {
		const Rule* r = rules_[ri];
		const nonterminal_t nt = r->nt;
		++stats_.candidates;

		BUE::BlockBase& sb = *scratch_[nt];
		if(not ops_[nt].eval(this, r, kp_.data(), sb, prune_mask_)) { ++stats_.pruned_error; return; }

		const BUE::FP fp = ops_[nt].fingerprint(sb);
		const uint32_t hit = tables_[nt].find(fp, [&](uint32_t id) {
			if(not params_.verify_equivalence) return true;
			BlockPtr other = get_block(nt, id);
			if(ops_[nt].equal(*other, sb)) return true;
			++stats_.collisions;
			return false;
		});

		if(hit != NONE) {
			++stats_.duplicates;
			Class& h = classes_[nt][hit];
			// same level => same cost: keep the higher exact prior (values are identical, so the cache stays valid)
			if(hit >= level_begin_[nt][level_] and lp > h.lp) {
				h.rule = ri;
				h.lp   = lp;
				h.kids = push_kids(r->N); // the old children become garbage in the arena (small)
				++stats_.replaced;
			}
			return;
		}

		if(stats_.classes >= params_.max_classes) { stopped_ = true; level_complete_ = false; return; }
		if(classes_[nt].size() >= NONE - 1) throw std::length_error("*** BottomUpEquivalence: too many classes");

		const uint32_t idx = uint32_t(classes_[nt].size());
		const uint32_t off = push_kids(r->N);
		classes_[nt].push_back(Class{ri, NONE, lp, off});
		tables_[nt].insert(fp, idx);
		++stats_.classes;
		BlockPtr b = ops_[nt].take(sb, inputs_.size());
		cache_insert(nt, idx, b);

		if(nt == out_nt() and matches_target(*b)) matches_.push_back(idx);
	}

	void product(uint32_t ri, size_t k, float lp) {
		const Rule* r = rules_[ri];
		if(k == r->N) { try_candidate(ri, lp); return; }
		const nonterminal_t nt = r->child_types[k];
		const auto [b, e] = level_range(nt, lv_[k]);
		for(uint32_t i = b; i < e and not stopped_; i++) {
			kid_[k] = i;
			kb_[k]  = get_block(nt, i);
			kp_[k]  = kb_[k].get();
			product(ri, k + 1, lp + classes_[nt][i].lp);
		}
		kb_[k].reset();
		kp_[k] = nullptr;
	}

	/// choose the levels of children k..N-1, which must sum to rem (each >= 1)
	void distribute(uint32_t ri, size_t k, uint32_t rem, float lp) {
		const Rule* r = rules_[ri];
		const size_t N = r->N;
		const nonterminal_t nt = r->child_types[k];
		auto nonempty = [&](uint32_t l) { auto [b, e] = level_range(nt, l); return b < e; };
		if(k == N - 1) {
			if(nonempty(rem)) { lv_[k] = rem; product(ri, 0, lp); }
			return;
		}
		for(uint32_t l = 1; l + (N - 1 - k) <= rem and not stopped_; l++) {
			if(not nonempty(l)) continue;
			lv_[k] = l;
			distribute(ri, k + 1, rem - l, lp);
		}
	}

	void check_with_vm(const Node& n, const BUE::Block<output_t>& b) const {
		for(size_t i = 0; i < inputs_.size(); i++) {
			VMS_t vm(inputs_[i], err_output_, nullptr);
			n.template linearize<VMS_t, Grammar_t>(vm.program);
			output_t o = vm.run();
			const bool vm_err = (vm.status == vmstatus_t::ERROR);
			const bool my_err = (b.err >> i) & 1;
			bool same = (vm_err == my_err);
			if(same and not vm_err) {
				if constexpr (std::equality_comparable<output_t>) same = (o == b.v[i]);
				else {
					BUE::Hasher h1, h2;
					BUE::Feed<output_t>::apply(h1, o); BUE::Feed<output_t>::apply(h2, b.v[i]);
					same = (h1.finish() == h2.finish());
				}
			}
			if(not same)
				throw std::logic_error("*** BottomUpEquivalence: Fleet's VM disagrees with bottom-up evaluation of " +
				                       n.string() + " on input " + std::to_string(i) +
				                       " (nondeterministic primitive, wrong Feed<T>, or unsupported op?)");
		}
	}

public:
	BottomUpEquivalence(const Grammar_t* g, std::vector<input_t> inputs, Params p = Params{})
		: grammar_(g), inputs_(std::move(inputs)), params_(p) {

		if(inputs_.empty() or inputs_.size() > MAX_EXAMPLES)
			throw std::invalid_argument("*** BottomUpEquivalence: need 1..64 inputs");
		if(not (params_.cost_quantum > 0.0) or not std::isfinite(params_.cost_quantum))
			throw std::invalid_argument("*** BottomUpEquivalence: cost_quantum must be positive and finite");

		vms_.resize(inputs_.size());
		for(size_t i = 0; i < inputs_.size(); i++) reset_vms(i);
		for(size_t nt = 0; nt < N_NTs; nt++) scratch_[nt] = ops_[nt].make(inputs_.size());

		for(size_t nt = 0; nt < N_NTs; nt++) {
			level_begin_[nt].push_back(0); // level 0 is empty
			for(const Rule& r : grammar_->rules[nt]) {
				if(not (r.p > 0.0)) continue; // can never be generated
				if(not (is_direct(r.op) or is_lazy(r.op)))
					throw std::invalid_argument("*** BottomUpEquivalence: unsupported op in rule " + r.format +
					                            " (only deterministic, non-recursive rules are allowed)");
				if(r.N > MAX_ARITY) throw std::invalid_argument("*** BottomUpEquivalence: arity too high in " + r.format);
				const double rlp = r.logp - grammar_->logZ[nt]; // log P(r | nt) <= 0
				const double q   = std::round(-rlp / params_.cost_quantum);
				if(not (q < 1e9)) throw std::invalid_argument("*** BottomUpEquivalence: rule cost overflow in " + r.format);
				rules_.push_back(&r);
				icost_.push_back(uint32_t(std::max(1.0, q)));
				rlp_.push_back(float(rlp));
				has_lazy_ = has_lazy_ or is_lazy(r.op);
			}
		}
		if(rules_.size() >= NONE) throw std::invalid_argument("*** BottomUpEquivalence: too many rules");
	}

	BottomUpEquivalence(const BottomUpEquivalence&)            = delete; // VM states reference err_output_
	BottomUpEquivalence& operator=(const BottomUpEquivalence&) = delete;

	/**
	 * @brief Set target outputs (nullopt = unknown, e.g. a test input). Must be called before run().
	 *        Only output-type classes that match all targets exactly are then reported.
	 */
	void set_targets(std::vector<std::optional<output_t>> t) {
		static_assert(std::equality_comparable<output_t>, "*** set_targets requires operator== on output_t");
		if(t.size() != inputs_.size()) throw std::invalid_argument("*** BottomUpEquivalence: targets/inputs size mismatch");
		if(level_ > 0) throw std::logic_error("*** BottomUpEquivalence: set_targets must precede run()");
		targets_ = std::move(t);
		target_mask_ = 0;
		for(size_t i = 0; i < targets_.size(); i++) if(targets_[i].has_value()) target_mask_ |= bit(i);
		prune_mask_ = (params_.prune_errors and not has_lazy_) ? target_mask_ : 0;
	}

	/**
	 * @brief Enumerate cost levels up to max_level (inclusive), calling cb for each reported output-type class
	 *        at the end of its level, in decreasing prior. Can be called repeatedly with larger max_level.
	 * @return true if all levels up to max_level were completed; false if stopped (callback returned false or
	 *         max_classes was reached). Once stopped, the instance cannot continue.
	 */
	bool run(uint32_t max_level, const Callback& cb) {
		while(level_ < max_level and not stopped_) {
			const uint32_t c = ++level_;
			for(size_t nt = 0; nt < N_NTs; nt++) level_begin_[nt].push_back(uint32_t(classes_[nt].size()));
			matches_.clear();

			for(uint32_t ri = 0; ri < rules_.size() and not stopped_; ri++) {
				if(icost_[ri] > c) continue;
				const size_t N = rules_[ri]->N;
				const uint32_t rem = c - icost_[ri];
				if(N == 0) { if(rem == 0) try_candidate(ri, rlp_[ri]); }
				else if(rem >= N) distribute(ri, 0, rem, rlp_[ri]);
			}

			std::sort(matches_.begin(), matches_.end(), [&](uint32_t x, uint32_t y) {
				const float a = classes_[out_nt()][x].lp, b = classes_[out_nt()][y].lp;
				return a != b ? a > b : x < y;
			});
			for(uint32_t idx : matches_) {
				const Node n = to_node(out_nt(), idx);
				BlockPtr b = get_block(out_nt(), idx);
				const auto& ob = as<output_t>(*b);
				if(params_.check_solutions) check_with_vm(n, ob);
				if(not cb(n, grammar_->log_probability(n), c, ob.v, ob.err)) { stopped_ = true; break; }
			}
		}
		return not stopped_;
	}

	/// Reconstruct the representative program of a class
	Node to_node(nonterminal_t nt, uint32_t idx) const {
		const Class& c = classes_[nt][idx];
		const Rule* r = rules_[c.rule];
		Node n = grammar_->makeNode(r);
		for(size_t k = 0; k < r->N; k++) n.set_child(k, to_node(r->child_types[k], arena_[c.kids + k]));
		return n;
	}

	uint32_t level()                 const { return level_; }
	bool     last_level_complete()   const { return level_complete_; }
	const Stats& stats()             const { return stats_; }
	size_t   num_classes(nonterminal_t nt) const { return classes_[nt].size(); }
	uint32_t rule_cost(const Rule* r) const {
		for(size_t i = 0; i < rules_.size(); i++) if(rules_[i] == r) return icost_[i];
		return NONE;
	}

	/// Values of class idx of type T (testing / inspection). The pointer keeps them alive.
	template<typename T>
	std::pair<BlockPtr, const BUE::Block<T>*> values(uint32_t idx) {
		constexpr nonterminal_t nt = Grammar_t::template nt<T>();
		BlockPtr b = get_block(nt, idx);
		return { b, &as<T>(*b) };
	}
	/// Level (= integer cost) of class idx of nonterminal nt
	uint32_t class_level(nonterminal_t nt, uint32_t idx) const {
		const auto& lb = level_begin_[nt];
		return uint32_t(std::upper_bound(lb.begin(), lb.end(), idx) - lb.begin()) - 1;
	}

	/// Approximate structural memory (excluding the value cache, which is in stats().cache_bytes)
	size_t structure_bytes() const {
		size_t s = arena_.size() * sizeof(uint32_t) + slots_.capacity() * sizeof(Slot);
		for(size_t nt = 0; nt < N_NTs; nt++) s += classes_[nt].size() * sizeof(Class) + tables_[nt].bytes();
		return s;
	}
};
