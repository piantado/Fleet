// Test for PartitionMCMC on FormalLanguageTheory-Simple: checks that get_partitions is an exact (disjoint,
// exhaustive) cover of the prior, and that run() yields every complete tree exactly once and never changes
// a chain's fixed prefix. Build from this directory with, e.g.:
//   g++ TestPartitionMCMC.cpp -o testpartition -O2 -pthread $(FLEET_FLAGS) $(FLEET_INCLUDE) -I../Models/FormalLanguageTheory-Simple -lm

#define DO_NOT_INCLUDE_MAIN
#include "Main.cpp"   // FormalLanguageTheory-Simple
#include "TopN.h"
#include "MCMCChain.h"
#include "ChainPool.h"
#include "Fleet.h"
#include "PartitionMCMC.h"
#include <map>

bool matches(const Node& p, const Node& t) { // t is a completion of partial tree p
	if(p.is_null()) return true;
	if(p.rule != t.rule or p.nchildren()!=t.nchildren()) return false;
	for(size_t i=0;i<p.nchildren();i++) if(not matches(p.child(i), t.child(i))) return false;
	return true;
}
bool same(const Node& a, const Node& b) { // identical rules everywhere
	if(a.rule != b.rule or a.nchildren()!=b.nchildren()) return false;
	for(size_t i=0;i<a.nchildren();i++) if(not same(a.child(i), b.child(i))) return false;
	return true;
}
bool fixed_preserved(const Node& init, const Node& t) {
	if(init.can_resample) return true;
	if(init.rule != t.rule or t.can_resample) return false;
	for(size_t i=0;i<init.nchildren();i++) if(not fixed_preserved(init.child(i), t.child(i))) return false;
	return true;
}

int main(int argc, char** argv) {
	Fleet fleet("test");
	fleet.initialize(argc, argv);
	for(const char c : alphabet) grammar.add_terminal(Q(S(1,c)), c, 3.0/alphabet.length());
	VirtualMachineControl::MAX_STEPS = 256; VirtualMachineControl::MAX_OUTPUTS = 256;
	auto mydata = str_to<std::vector<MyHypothesis::datum_t>>(datastr);
	MyHypothesis h0;
	bool ok = true;

	// ---- 1. exact cover: every prior sample is in exactly one partial partition XOR equal to exactly one complete tree
	for(auto [D, M] : std::vector<std::pair<size_t,size_t>>{{1,0},{2,0},{3,0},{4,0},{0,100},{5,100},{10,1}}) {
		auto P = get_partitions(h0, D, M);
		std::map<size_t,size_t> lev; for(auto& p: P.partial) lev[p.get_value().count_nonnull()]++;
		size_t nested=0;
		for(size_t i=0;i<P.partial.size();i++) for(size_t j=0;j<P.partial.size();j++)
			if(i!=j and matches(P.partial[i].get_value(), P.partial[j].get_value())) nested++;
		size_t dup=0; for(size_t i=0;i<P.complete.size();i++) for(size_t j=i+1;j<P.complete.size();j++) dup += same(P.complete[i].get_value(), P.complete[j].get_value());
		size_t N=100000, bad=0, n_in_complete=0;
		for(size_t k=0;k<N;k++) {
			auto t = grammar.generate();
			size_t c=0, cc=0;
			for(auto& p: P.partial)  c  += matches(p.get_value(), t);
			for(auto& p: P.complete) cc += same(p.get_value(), t);
			bad += (c+cc != 1); n_in_complete += cc;
		}
		print("D=",D,"max_size=",M," partial=",P.partial.size()," complete=",P.complete.size(),
		      " partial levels(#nodes:count)=", str(lev), " nested=",nested," dup_complete=",dup,
		      " | prior samples not covered exactly once:", bad, "/", N, " in complete:", n_in_complete/(double)N);
		ok = ok and bad==0 and nested==0 and dup==0 and lev.size()==1;
	}

	// ---- 2. run(): each complete tree is yielded exactly once (even with steps=1 and 2 threads) & fixed prefixes are kept
	FleetArgs::MCMCYieldOnlyChanges = false;
	for(size_t nthr : {1,2}) for(unsigned long steps : {1ul, 50000ul}) {
		PartitionMCMC samp(h0, 3, mydata);
		std::vector<Node> init; for(auto& c: samp.pool) init.push_back(c.getCurrent().get_value());
		std::vector<size_t> seen(samp.complete_trees.size(), 0);
		size_t nchain=0, viol=0, badpost=0;
		for(auto& h : samp.run(Control(steps, 0, nthr))) {
			if(h.born_chain_idx == PartitionMCMC<MyHypothesis>::COMPLETE_TREE_IDX) {
				size_t hits=0;
				for(size_t i=0;i<seen.size();i++) if(same(samp.complete_trees[i].get_value(), h.get_value())) { seen[i]++; hits++; }
				auto hc = h; hc.compute_posterior(mydata); // posterior must have been computed correctly
				if(hits!=1 or not (hc.posterior == h.posterior or (std::isnan(hc.posterior) and std::isnan(h.posterior)))) badpost++;
			} else {
				nchain++; viol += not fixed_preserved(init.at(h.born_chain_idx), h.get_value());
			}
		}
		size_t once = std::count(seen.begin(), seen.end(), 1);
		print("nthreads=",nthr," steps=",steps," complete yielded exactly once:", once, "/", seen.size(),
		      " chain samples:", nchain, " fixed-prefix violations:", viol, " bad complete yields:", badpost);
		ok = ok and once==seen.size() and viol==0 and badpost==0;
	}
	// ---- 3. a complete tree with the true answer's shape gets the posterior it should
	print(ok ? "ALL OK" : "FAILURES");
	return ok ? 0 : 1;
}
