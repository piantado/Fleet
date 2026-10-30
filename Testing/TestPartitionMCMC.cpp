// TestPartitionMCMC: get_partitions and PartitionMCMC on FiniteGrammar (309 programs, exact posteriors).
//
//  1. get_partitions is an exact cover: for many (max_depth, max_size), every one of the 309 programs is
//     either exactly one of the `complete` trees or a completion of exactly one `partial` tree. The partial
//     trees all sit at one level (same number of expanded nodes), none contains another, there are no
//     duplicates, max_size is respected, and a depth beyond the space returns all 309 programs as complete.
//  2. The PartitionMCMC constructor: one chain per partial tree, whose start keeps the partition's prefix
//     (can_resample=false) and fills every gap (can_resample=true); complete trees carry exact posteriors.
//  3. run(): every complete tree is yielded exactly once (1-3 threads, with tiny and larger step budgets,
//     and when everything is complete so there are no chains); every yielded sample carries its exact
//     posterior; chain samples are always completions of their own partition with the prefix untouched.
//  4. Each chain samples the exact posterior RESTRICTED to its partition (replicate PartitionMCMC runs
//     compared with the enumerated restricted posterior).
//  5. set_data recomputes the complete trees' posteriors (and the chains').
//  6. Calling PartitionMCMC on a complete tree asserts.

#include "TestModels.h"
#include "ChainPool.h"
#include "PartitionMCMC.h"

using namespace TestHarness;

using H  = FiniteHypothesis;
using PM = PartitionMCMC<H>;

// is the complete tree t a completion of the partial tree p? (null nodes of p are gaps)
bool completes(const Node& p, const Node& t) {
	if(p.is_null()) return true;
	if(p.rule != t.rule or p.nchildren() != t.nchildren()) return false;
	for(size_t i=0;i<p.nchildren();i++) if(not completes(p.child(i), t.child(i))) return false;
	return true;
}

// does t keep the fixed (can_resample=false) prefix of the chain's start tree s, still unresampleable?
// And is everything below that prefix resampleable?
bool keeps_prefix(const Node& s, const Node& t) {
	if(s.can_resample) {
		for(const auto& n : t) if(not n.can_resample) return false;
		return true;
	}
	if(s.rule != t.rule or t.can_resample or s.nchildren() != t.nchildren()) return false;
	for(size_t i=0;i<s.nchildren();i++) if(not keeps_prefix(s.child(i), t.child(i))) return false;
	return true;
}

// the partition (partial tree, with gaps) that the chain's start tree s was made from
Node prefix_of(const Node& s) {
	if(s.can_resample) return Node(); // a gap
	Node out = s;
	for(size_t i=0;i<s.nchildren();i++) out.set_child(i, prefix_of(s.child(i)));
	out.can_resample = true; // so that string/parseable are not decorated
	return out;
}

double exact_post(const std::map<std::string, ExactState>& states, const Node& n) {
	const auto& st = states.at(key(n));
	return st.prior + st.loglik;
}

int main() {
	test_name = "PartitionMCMC";
	FleetArgs::print_header = 0;
	FleetArgs::MCMCYieldOnlyChanges = false;
	DefaultRNG.seed(seed());
	auto states = finite_states();
	CHECK(states.size() == 309, "FiniteGrammar has 309 programs, got " << states.size());

	H h0; // empty

	// ==================================================================================
	section("get_partitions is an exact, disjoint cover of all 309 programs");
	{
		const std::vector<std::pair<size_t,size_t>> settings = {
			{1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {50,0},
			{0,1}, {0,5}, {0,7}, {0,20}, {0,100}, {0,1000},
			{2,3}, {3,7}, {4,30}, {10,1}
		};
		for(auto [D, M] : settings) {
			const std::string lab = "max_depth=" + str(D) + " max_size=" + str(M);
			auto P = get_partitions(h0, D, M);

			// every program covered exactly once
			size_t bad = 0;
			for(auto& [k, st] : states) {
				size_t c = 0;
				for(auto& p : P.partial)  c += completes(p.get_value(), st.node);
				for(auto& p : P.complete) c += (key(p.get_value()) == k);
				bad += (c != 1);
			}
			CHECK(bad == 0, lab << ": " << bad << " of 309 programs are not covered exactly once");

			// structure of the partial trees
			std::set<size_t> levels; std::set<std::string> pkeys;
			size_t nested = 0, evaluable = 0;
			for(auto& p : P.partial) {
				levels.insert(p.get_value().count_nonnull());
				pkeys.insert(p.get_value().string());
				evaluable += p.is_evaluable();
			}
			for(size_t i=0;i<P.partial.size();i++) for(size_t j=0;j<P.partial.size();j++)
				if(i != j and completes(P.partial[i].get_value(), P.partial[j].get_value())) nested++;
			CHECK(levels.size() <= 1, lab << ": partial trees come from " << levels.size() << " different levels");
			CHECK(nested == 0, lab << ": " << nested << " pairs of partial trees contain one another");
			CHECK(evaluable == 0, lab << ": " << evaluable << " partial trees are actually complete");
			CHECK(pkeys.size() == P.partial.size(), lab << ": duplicate partial trees");
			if(M > 0) CHECK(P.partial.size() <= std::max<size_t>(M,1), lab << ": " << P.partial.size() << " partitions exceeds max_size");
			if(D > 0 and not levels.empty()) CHECK(*levels.begin() <= D, lab << ": partial trees deeper than max_depth");

			// structure of the complete trees
			std::set<std::string> ckeys; size_t not_complete = 0, too_big = 0;
			const size_t lvl = levels.empty() ? std::numeric_limits<size_t>::max() : *levels.begin();
			for(auto& c : P.complete) {
				ckeys.insert(key(c.get_value()));
				not_complete += not c.is_evaluable();
				too_big += (c.get_value().count() > lvl); // completed at or before the final level
			}
			CHECK(ckeys.size() == P.complete.size(), lab << ": duplicate complete trees");
			CHECK(not_complete == 0, lab << ": " << not_complete << " 'complete' trees have gaps");
			CHECK(too_big == 0, lab << ": " << too_big << " complete trees are bigger than the final partition level");
			note(lab + ": " + str(P.partial.size()) + " partitions, " + str(P.complete.size()) + " complete trees");
		}

		// expanding past the whole (finite) space: no partitions, every program complete
		auto P = get_partitions(h0, 50, 0);
		CHECK(P.partial.empty() and P.complete.size() == 309, "deep expansion gives 0 partitions and 309 complete trees, got "
		      << P.partial.size() << " and " << P.complete.size());
	}

	// ==================================================================================
	section("constructor: chains start inside their partitions; complete trees have exact posteriors");
	for(size_t D : {1ul, 2ul, 3ul}) {
		auto P = get_partitions(h0, D, 0);
		PM samp(h0, D, finite_data);
		CHECK(samp.pool.size() == P.partial.size(), "D=" << D << ": " << samp.pool.size() << " chains for " << P.partial.size() << " partitions");
		CHECK(samp.complete_trees.size() == P.complete.size(), "D=" << D << ": complete tree count");

		std::set<std::string> want; for(auto& p : P.partial) want.insert(p.get_value().string());
		std::set<std::string> got;
		size_t bad_start = 0, bad_post = 0;
		for(auto& c : samp.pool) {
			const Node& s = c.getCurrent().get_value();
			got.insert(prefix_of(s).string());
			bad_start += not (s.is_complete() and keeps_prefix(s, s) and completes(prefix_of(s), s));
			bad_post  += not close(c.getCurrent().posterior, exact_post(states, s), 1e-12, 1e-12);
		}
		CHECK(got == want, "D=" << D << ": the chains' fixed prefixes are not exactly the partitions");
		CHECK(bad_start == 0, "D=" << D << ": " << bad_start << " chains start outside their partition or with gaps");
		CHECK(bad_post == 0, "D=" << D << ": " << bad_post << " chains start with a wrong posterior");

		size_t bad_c = 0;
		for(auto& h : samp.complete_trees) {
			bad_c += not (h.born_chain_idx == PM::COMPLETE_TREE_IDX and close(h.posterior, exact_post(states, h.get_value()), 1e-12, 1e-12));
		}
		CHECK(bad_c == 0, "D=" << D << ": " << bad_c << " complete trees have a wrong posterior or chain index");
	}

	// ==================================================================================
	section("run(): every complete tree is yielded exactly once; chain samples stay in their partitions");
	for(size_t D : {1ul, 2ul, 3ul, 50ul}) for(size_t nthr : {1ul, 2ul, 3ul}) for(unsigned long steps : {1ul, 3000ul, 30000ul}) {
		const std::string lab = "D=" + str(D) + " threads=" + str(nthr) + " steps=" + str(steps);
		PM samp(h0, D, finite_data);
		if(samp.pool.size() > 0 and samp.pool.size() < nthr) continue; // ChainPool requires nthreads <= #chains

		std::vector<Node> start; for(auto& c : samp.pool) start.push_back(c.getCurrent().get_value());
		std::map<std::string,size_t> seen; for(auto& h : samp.complete_trees) seen[key(h.get_value())] = 0;

		size_t nchain = 0, outside = 0, bad_post = 0, unknown = 0;
		for(auto& h : samp.run(Control(steps, 0, nthr))) {
			if(not close(h.posterior, exact_post(states, h.get_value()), 1e-12, 1e-12)) bad_post++;
			if(h.born_chain_idx == PM::COMPLETE_TREE_IDX) {
				auto it = seen.find(key(h.get_value()));
				if(it == seen.end()) unknown++; else it->second++;
			}
			else {
				nchain++;
				if(h.born_chain_idx >= start.size() or not keeps_prefix(start[h.born_chain_idx], h.get_value())) outside++;
			}
		}
		size_t once = 0; for(auto& [k,c] : seen) once += (c == 1);
		CHECK(once == seen.size() and unknown == 0, lab << ": only " << once << " of " << seen.size()
		      << " complete trees yielded exactly once (" << unknown << " unknown)");
		CHECK(outside == 0, lab << ": " << outside << " chain samples left their partition");
		CHECK(bad_post == 0, lab << ": " << bad_post << " samples carry a wrong posterior");
		if(samp.pool.empty()) CHECK(nchain == 0 and seen.size() == 309, lab << ": with no chains, run must yield exactly the 309 programs");
		else if(steps >= 3000) CHECK(nchain > 0, lab << ": no chain samples");
	}

	// ==================================================================================
	section("each chain samples the exact posterior restricted to its partition");
	{
		const size_t D = 2;
		const int K = 32;
		PM proto(h0, D, finite_data);
		const size_t NC = proto.pool.size();
		std::vector<Node> parts; for(auto& c : proto.pool) parts.push_back(prefix_of(c.getCurrent().get_value()));
		const unsigned long steps_per_chain = 60000;

		// K independent PartitionMCMC runs; replicate k's counts for chain i are stored under "i|program"
		auto reps = replicate(K, [&](int){
			PM samp(h0, D, finite_data);
			Counts c;
			for(auto& h : samp.run(Control(steps_per_chain*NC, 0, 1))) {
				if(h.born_chain_idx != PM::COMPLETE_TREE_IDX) c[str(h.born_chain_idx) + "|" + key(h.get_value())]++;
			}
			return c;
		});

		size_t tested = 0;
		for(size_t i=0;i<NC;i++) {
			auto keep = [&](const Node& n){ return completes(parts[i], n); };
			auto exact = exact_posterior(states, 1.0, keep);
			if(exact.size() < 10) continue; // too few states to be informative
			tested++;
			// pull out chain i's counts, keyed by program
			std::vector<Counts> ri(K);
			for(int k=0;k<K;k++) for(auto& [s,c] : reps[k]) {
				auto bar = s.find('|');
				if(s.substr(0,bar) == str(i)) ri[k][s.substr(bar+1)] += c;
			}
			compare_to_exact(ri, exact, "partition " + parts[i].string() + " (" + str(exact.size()) + " programs)");
		}
		CHECK(tested >= 2, "expected at least two partitions with >= 10 programs at depth 2, got " << tested);
	}

	// ==================================================================================
	section("set_data recomputes complete trees and chains");
	{
		static std::vector<H::datum_t> other_data = { H::datum_t((short)1, 7), H::datum_t((short)3, 3) }; // must outlive the chains
		PM samp(h0, 2, finite_data);
		samp.set_data(other_data);
		size_t bad = 0;
		for(auto& h : samp.complete_trees) {
			const Node& n = h.get_value();
			bad += not close(h.posterior, states.at(key(n)).prior + ref_loglik(n, other_data), 1e-12, 1e-12);
		}
		for(auto& c : samp.pool) {
			const Node& n = c.getCurrent().get_value();
			bad += not close(c.getCurrent().posterior, states.at(key(n)).prior + ref_loglik(n, other_data), 1e-12, 1e-12);
		}
		CHECK(bad == 0, bad << " posteriors not recomputed by set_data");
	}

	// ==================================================================================
	section("PartitionMCMC on a complete tree asserts");
	{
		CHECK(dies([]{ H h(finite_grammar.generate()); PM samp(h, 2, finite_data); }), "PartitionMCMC on a complete tree should assert");
	}

	return finish();
}
