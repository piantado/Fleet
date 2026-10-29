// TestMCMC: MCMCChain and the proposal kernels' forward-backward (Metropolis-Hastings) corrections.
//
//  1. regenerate (LOTHypothesis's default proposal), checked PATH BY PATH: for every proposal a->b
//     we enumerate the node positions P at which regenerating could have produced b, compute
//         fb_P = [log 1/Z(a) + lp(b_P)] - [log 1/Z(b) + lp(a_P)]      (Z = # resampleable nodes)
//     independently, and require the reported fb to equal one of them. Includes trees with
//     can_resample=false nodes (where Z(a) != Z(b) matters).
//  2. The regeneration proposal DISTRIBUTION from fixed states, against the exact q(a->b)
//     (chi-square), and Proposals::p_regeneration_propose_to against the exact q.
//  3. sample_function_leaving_args: fb and the per-node lp it writes.
//  4. Stationary distribution of MCMCChain against the exact posterior of a 309-program model
//     (enumerated; priors and likelihoods computed independently), with replicate chains:
//       - default proposals, temperature 1
//       - temperature 3 (target prior * likelihood^(1/3))
//       - root fixed with can_resample=false (target = posterior restricted to that root)
//  5. A recursive grammar with a mixture of all five proposal kernels (regenerate, insert_tree,
//     delete_tree, sample_function_leaving_args, swap_args): posterior ratios of common states.
//  6. Proposals that fail (return no proposal) with state-dependent probability.
//  7. Mechanics: breakout gives bit-identical trajectories, step counts, bookkeeping, copies.

#include "TestModels.h"
#include "Proposers.h"

using namespace TestHarness;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// A recursive grammar for proposal checks and the mixture-kernel chain
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class RecGrammar : public Grammar<int,int, int,bool> {
public:
	RecGrammar() {
		add("add(%s,%s)",     +[](int a, int b) -> int { return a+b; }, 1.0);
		add("sub(%s,%s)",     +[](int a, int b) -> int { return a-b; }, 0.5);
		add("neg(%s)",        +[](int a) -> int { return -a; }, 0.4);
		add("x",              Builtins::X<RecGrammar>, 2.0);
		add("one",            +[]() -> int { return 1; }, 1.5);
		add("if(%s,%s,%s)",   Builtins::If<RecGrammar,int>, 0.3);
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a<b; }, 0.5);
		add("and(%s,%s)",     Builtins::And<RecGrammar>, 0.3);
		add("or(%s,%s)",      Builtins::Or<RecGrammar>, 0.3);
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("F",              +[]() -> bool { return false; }, 1.0);
	}
} rec_grammar;

const Spec rec_spec = {{
	{0,"add(%s,%s)",{0,0},1.0}, {0,"sub(%s,%s)",{0,0},0.5}, {0,"neg(%s)",{0},0.4}, {0,"x",{},2.0}, {0,"one",{},1.5}, {0,"if(%s,%s,%s)",{1,0,0},0.3},
	{1,"lt(%s,%s)",{0,0},0.5}, {1,"and(%s,%s)",{1,1},0.3}, {1,"or(%s,%s)",{1,1},0.3}, {1,"T",{},1.0}, {1,"F",{},1.0}
}};

long rec_eval(const Node& n, long x) {
	const std::string& f = n.rule->format;
	if(f == "add(%s,%s)")    return rec_eval(n.child(0),x) + rec_eval(n.child(1),x);
	if(f == "sub(%s,%s)")    return rec_eval(n.child(0),x) - rec_eval(n.child(1),x);
	if(f == "neg(%s)")       return -rec_eval(n.child(0),x);
	if(f == "x")             return x;
	if(f == "one")           return 1;
	if(f == "if(%s,%s,%s)")  return rec_eval(n.child(0),x) ? rec_eval(n.child(1),x) : rec_eval(n.child(2),x);
	if(f == "lt(%s,%s)")     return rec_eval(n.child(0),x) < rec_eval(n.child(1),x);
	if(f == "and(%s,%s)")    return rec_eval(n.child(0),x) and rec_eval(n.child(1),x);
	if(f == "or(%s,%s)")     return rec_eval(n.child(0),x) or rec_eval(n.child(1),x);
	if(f == "T")             return 1;
	if(f == "F")             return 0;
	throw std::runtime_error("rec_eval: " + f);
}

constexpr double REC_ALPHA = 0.3;

// Mixture of all proposal kernels. A kernel that cannot propose from this state yields the
// IDENTITY proposal (stay, fb=0) -- the correct way to keep the mixture a valid MH kernel.
class RecHypothesis : public DeterministicLOTHypothesis<RecHypothesis, int, int, RecGrammar, &rec_grammar> {
public:
	using Super = DeterministicLOTHypothesis<RecHypothesis, int, int, RecGrammar, &rec_grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override {
		return std::log(REC_ALPHA*(call(d.input, -999) == d.output) + (1-REC_ALPHA)/NOUT);
	}
	[[nodiscard]] ProposalType propose() const override {
		std::optional<std::pair<Node,double>> p;
		const double u = uniform();
		if(u < 0.4)       p = Proposals::regenerate(&rec_grammar, value);
		else if(u < 0.55) p = Proposals::insert_tree(&rec_grammar, value);
		else if(u < 0.70) p = Proposals::delete_tree(&rec_grammar, value);
		else if(u < 0.85) p = Proposals::sample_function_leaving_args(&rec_grammar, value);
		else              p = Proposals::swap_args(&rec_grammar, value);
		if(not p) return std::make_pair(*this, 0.0);
		RecHypothesis ret(std::move(p->first));
		return std::make_pair(std::move(ret), p->second);
	}
};

std::vector<RecHypothesis::datum_t> rec_data = { RecHypothesis::datum_t(0, 1), RecHypothesis::datum_t(1, 2), RecHypothesis::datum_t(3, 4) };

double rec_logpost(const Node& n) {
	double ll = 0;
	for(auto& d : rec_data) ll += std::log(REC_ALPHA*(rec_eval(n, d.input) == d.output) + (1-REC_ALPHA)/NOUT);
	return rec_spec.tree_lp(n) + ll;
}

// A FiniteHypothesis with a 50/50 mixture of two kernels, each a valid MH kernel on its own:
//   A: regeneration (always succeeds)
//   B: an independence proposal from the prior restricted to programs whose root is NOT plus(.,.)
//      (fb = lp(new) - lp(old)); B cannot propose (returns no proposal) from plus-rooted programs --
//      just as swap_args or sample_function_leaving_args cannot propose from many trees (and, like
//      them, B never proposes INTO such states, so B by itself is a valid reversible kernel).
// As a Markov chain this is valid if a failed proposal means "stay this step". If instead the
// step is silently retried, the mixture weights become state-dependent (A only, at plus-rooted
// programs) and the chain no longer targets the posterior. This is how Fleet's models mix kernels.
template<bool IDENTITY_ON_FAILURE>
class MixtureHypothesis : public DeterministicLOTHypothesis<MixtureHypothesis<IDENTITY_ON_FAILURE>, short, int, FiniteGrammar, &finite_grammar> {
public:
	using Super = DeterministicLOTHypothesis<MixtureHypothesis<IDENTITY_ON_FAILURE>, short, int, FiniteGrammar, &finite_grammar>;
	using Super::Super;
	using typename Super::datum_t;
	using typename Super::ProposalType;
	double compute_single_likelihood(const datum_t& d) override {
		return std::log(ALPHA*(this->call(d.input, -999) == d.output) + (1-ALPHA)/NOUT);
	}
	[[nodiscard]] ProposalType propose() const override {
		if(flip(0.5)) return Super::propose();
		if(this->value.rule->format == "plus(%s,%s)") {
			if constexpr (IDENTITY_ON_FAILURE) return std::make_pair(*this, 0.0);
			else return {};
		}
		// independence proposal from the prior. (NOTE: Proposals::prior_proposal does exactly this but
		// does not compile when instantiated -- `return {g, ...}` cannot initialize a std::optional<std::pair>.)
		// B is closed on the non-plus programs (it neither leaves nor enters plus-rooted ones), so B
		// alone is reversible w.r.t. the posterior; the restricted prior's normalizer cancels in fb.
		Node g; do { g = finite_grammar.generate(); } while(g.rule->format == "plus(%s,%s)");
		const double fb = finite_grammar.log_probability(g) - finite_grammar.log_probability(this->value);
		return std::make_pair(MixtureHypothesis(std::move(g)), fb);
	}
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Paths into trees
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

using Path = std::vector<size_t>;

void all_paths(const Node& n, Path& cur, std::vector<Path>& out) {
	out.push_back(cur);
	for(size_t i=0;i<n.nchildren();i++) { cur.push_back(i); all_paths(n.child(i), cur, out); cur.pop_back(); }
}
std::vector<Path> all_paths(const Node& n) { std::vector<Path> out; Path p; all_paths(n, p, out); return out; }

template<typename N>
N* at(N& n, const Path& p) {
	N* x = &n;
	for(size_t i : p) { if(i >= x->nchildren()) return nullptr; x = &x->child(i); }
	return x;
}

// the full state of a tree including can_resample flags
std::string full(const Node& n) {
	std::string s = n.rule->format + (n.can_resample ? "" : "!");
	if(n.nchildren()) { s += "("; for(size_t i=0;i<n.nchildren();i++) s += (i?",":"") + full(n.child(i)); s += ")"; }
	return s;
}

double nresample(const Node& n) { double c = 0; for(auto& m : n) c += m.can_resample; return c; }

// all pathwise regeneration fb values that could have produced a->b
template<typename G>
std::vector<double> regeneration_path_fbs(G& g, const Node& a, const Node& b) {
	std::vector<double> out;
	const double za = nresample(a), zb = nresample(b);
	for(const Path& P : all_paths(a)) {
		const Node* ap = at(a, P);
		if(not ap->can_resample) continue;
		const Node* bp = at(b, P);
		if(bp == nullptr or bp->nt() != ap->nt()) continue;
		Node a2 = a;
		at(a2, P)->assign(Node(*bp));
		if(full(a2) != full(b)) continue;
		out.push_back((-std::log(za) + g.log_probability(*bp)) - (-std::log(zb) + g.log_probability(*ap)));
	}
	return out;
}

// the exact regeneration proposal distribution from a, over full() keys (finite grammar only)
std::map<std::string,double> exact_regeneration_q(const Node& a) {
	std::map<std::string,double> q;
	const double za = nresample(a);
	for(const Path& P : all_paths(a)) {
		const Node* ap = at(a, P);
		if(not ap->can_resample) continue;
		for(auto& [u, lpu] : enumerate_trees(finite_grammar, finite_spec, ap->nt())) {
			Node b = a;
			at(b, P)->assign(Node(u));
			q[full(b)] += std::exp(lpu)/za;
		}
	}
	return q;
}

Node random_rec_tree(size_t maxn=30) {
	while(true) { Node t = rec_grammar.generate(); if(t.count() <= maxn) return t; }
}

int main() {
	test_name = "MCMC";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("regenerate: every fb equals a pathwise MH correction");
	{
		int bad = 0, total = 0, nofb = 0;
		for(int r=0;r<20000;r++) {
			Node a = random_rec_tree();
			if(r % 2) for(auto& n : a) if(flip(0.3)) n.can_resample = false; // half the trees have fixed nodes
			auto p = Proposals::regenerate(&rec_grammar, a);
			if(nresample(a) == 0) { CHECK(not p, "regenerate with no resampleable nodes returns nothing"); continue; }
			if(not p) { CHECK(false, "regenerate returned nothing although nodes are resampleable"); continue; }
			const Node& b = p->first;
			auto fbs = regeneration_path_fbs(rec_grammar, a, b);
			total++;
			if(fbs.empty()) { nofb++; if(nofb <= 3) CHECK(false, "no position of " << full(a) << " regenerates to " << full(b)); continue; }
			bool match = false;
			for(double f : fbs) match = match or std::abs(f - p->second) < 1e-9;
			if(not match and bad++ < 3) CHECK(false, "fb " << p->second << " matches no path for " << full(a) << " -> " << full(b));
		}
		CHECK(bad == 0, bad << " of " << total << " regenerate proposals had an fb matching no path");
		CHECK(nofb == 0, nofb << " regenerate proposals were not reachable by regenerating one node");
	}

	// ==================================================================================
	section("regenerate: proposal distribution from fixed states vs exact q(a->b)");
	{
		auto states = finite_states();
		std::vector<Node> starts;
		for(auto& [k, s] : states) if(s.node.count() >= 3) starts.push_back(s.node);
		int pr_mismatch = 0, pr_checked = 0; std::string pr_example;
		for(int which=0; which<6; which++) {
			Node a = starts[myrandom(starts.size())];
			if(which >= 3) { // some nodes fixed
				std::vector<Node*> nodes; for(auto& n : a) nodes.push_back(&n);
				nodes[myrandom(nodes.size())]->can_resample = false;
			}
			auto q = exact_regeneration_q(a);
			double qs = 0; for(auto& [k,v] : q) qs += v;
			CHECK_CLOSE(qs, 1.0, 1e-12, 0, "exact q sums to 1");
			std::map<std::string,double> obs;
			const int N = 400000;
			bool fb_ok = true;
			for(int i=0;i<N;i++) {
				auto p = Proposals::regenerate(&finite_grammar, a);
				obs[full(p->first)]++;
				if(i < 2000) {
					auto fbs = regeneration_path_fbs(finite_grammar, a, p->first);
					bool m = false; for(double f : fbs) m = m or std::abs(f - p->second) < 1e-9;
					fb_ok = fb_ok and m;
				}
			}
			CHECK(fb_ok, "pathwise fb from " << full(a));
			std::vector<double> o, e; bool outside = false;
			for(auto& [k,v] : obs) if(not q.count(k)) outside = true;
			for(auto& [k,v] : q) { o.push_back(obs[k]); e.push_back(v); }
			CHECK(not outside, "regenerate proposed something unreachable from " << full(a));
			CHECK_PVALUE(chisq_test(o, e, "regenerate from " + full(a)), "regeneration proposal distribution from " << full(a));

			// p_regeneration_propose_to is documented as "probability of proposing from a to b under
			// regeneration" (and documented NOT to handle can_resample, so only test those cases)
			if(which < 3) {
				for(auto& [k, qv] : q) {
					if(qv < 1e-6) continue;
					// rebuild b from the key: find it among single-position regenerations
					for(const Path& P : all_paths(a)) {
						for(auto& [u, lpu] : enumerate_trees(finite_grammar, finite_spec, at(a,P)->nt())) {
							Node b = a; at(b,P)->assign(Node(u));
							if(full(b) != k) continue;
							const double got = Proposals::p_regeneration_propose_to(&finite_grammar, a, b);
							pr_checked++;
							if(std::abs(got - std::log(qv)) > 1e-5) { pr_mismatch++; if(pr_example.empty()) pr_example = full(a) + " -> " + k + ": " + str(got) + " vs exact " + str(std::log(qv)); }
							goto next_key;
						}
					}
					next_key:;
				}
			}
		}
		CHECK(pr_mismatch == 0, "Proposals::p_regeneration_propose_to disagrees with the exact proposal probability for "
		      << pr_mismatch << " of " << pr_checked << " (a,b) pairs, e.g. " << pr_example
		      << ". (Its recursive call adds -log(count) of the SUBTREE on top of the outer -log(count), so proposals "
		      "inside a subtree are double-penalized.)");
	}

	// ==================================================================================
	section("sample_function_leaving_args: fb and node lp");
	{
		int checked = 0, fb_bad = 0, lp_bad = 0;
		for(int r=0;r<20000;r++) {
			Node a = random_rec_tree();
			auto p = Proposals::sample_function_leaving_args(&rec_grammar, a);
			if(not p) continue;
			const Node& b = p->first;
			// exactly one node changed its rule, keeping the same children
			const Node* changed_a = nullptr; const Node* changed_b = nullptr; int nchanged = 0;
			for(const Path& P : all_paths(a)) {
				const Node* x = at(a,P); const Node* y = at(b,P);
				if(y == nullptr or x->nchildren() != y->nchildren()) { nchanged = 99; break; }
				if(x->rule != y->rule) { nchanged++; changed_a = x; changed_b = y; }
			}
			if(nchanged == 0) { CHECK(std::abs(p->second) < 1e-15, "no-change proposal must have fb 0"); continue; }
			if(nchanged != 1) { CHECK(false, "sample_function_leaving_args changed more than one rule"); continue; }
			checked++;
			// forward: pick the node (1/Z) and the rule p_new/S; backward: same node (same Z), p_old/S
			const double expect = std::log(changed_b->rule->p) - std::log(changed_a->rule->p);
			if(std::abs(p->second - expect) > 1e-12) fb_bad++;
			if(std::abs(changed_b->lp - (changed_b->rule->logp - rec_grammar.logZ[changed_b->nt()])) > 1e-12) lp_bad++;
		}
		CHECK(checked > 1000, "enough sample_function_leaving_args proposals");
		CHECK(fb_bad == 0, fb_bad << " sample_function_leaving_args fbs differ from log p_new - log p_old");
		CHECK(lp_bad == 0, lp_bad << " of " << checked << " nodes changed by sample_function_leaving_args have lp = p/Z (a PROBABILITY) "
		      "instead of log(p/Z) like every other node (Proposers.h sets s->lp = p / Z)");
	}

	// ==================================================================================
	section("MCMCChain stationary distribution: default proposals, exact posterior");
	auto states = finite_states();
	const int K = 32;
	{
		auto exact = exact_posterior(states, 1.0);
		double mx = 0; for(auto& [k,v] : exact) mx = std::max(mx, v);
		note("exact posterior over " + str(exact.size()) + " programs, max probability " + str(mx));
		auto reps = replicate(K, [](int){
			return run_chain_counts(FiniteHypothesis(finite_grammar.generate()), finite_data, 150000);
		});
		compare_to_exact(reps, exact, "MCMCChain T=1");
	}

	// ==================================================================================
	section("MCMCChain at temperature 3: target prior * likelihood^(1/3)");
	{
		auto exact = exact_posterior(states, 3.0);
		auto reps = replicate(K, [](int){
			return run_chain_counts(FiniteHypothesis(finite_grammar.generate()), finite_data, 100000, 3.0);
		});
		compare_to_exact(reps, exact, "MCMCChain T=3");
	}

	// ==================================================================================
	section("MCMCChain with a fixed (can_resample=false) root: restricted posterior");
	{
		auto keep = [](const Node& n){ return n.rule->format == "ite(%s,%s,%s)"; };
		auto exact = exact_posterior(states, 1.0, keep);
		auto reps = replicate(K, [&](int){
			Node t; do { t = finite_grammar.generate(); } while(not keep(t));
			t.can_resample = false;
			return run_chain_counts(FiniteHypothesis(t), finite_data, 100000);
		});
		compare_to_exact(reps, exact, "MCMCChain fixed root");
	}

	// ==================================================================================
	section("MCMCChain with all five proposal kernels on a recursive grammar: posterior ratios");
	{
		const int KR = 32;
		auto reps = replicate(KR, [](int){
			RecHypothesis h0(random_rec_tree(5));
			return run_chain_counts(h0, rec_data, 400000);
		});
		// reference state = most visited
		Counts pooled; for(auto& r : reps) for(auto& [k,v] : r) pooled[k] += v;
		std::string ref; double refc = 0; for(auto& [k,v] : pooled) if(v > refc) { refc = v; ref = k; }
		const double lref = rec_logpost(rec_grammar.from_parseable(ref));
		int tested = 0, bad = 0; double worst = 0; std::string worst_s;
		for(auto& [s, c] : pooled) {
			if(c < 3000 or s == ref) continue;
			const double ltrue = rec_logpost(rec_grammar.from_parseable(s)) - lref;
			// jackknife over replicates for the SE of log(sum c(s) / sum c(ref))
			std::vector<double> cs(KR), cr(KR); double S=0, Rr=0;
			for(int k=0;k<KR;k++) { cs[k] = reps[k].count(s) ? reps[k].at(s) : 0; cr[k] = reps[k].count(ref) ? reps[k].at(ref) : 0; S += cs[k]; Rr += cr[k]; }
			const double est = std::log(S/Rr);
			double jm = 0; std::vector<double> jk(KR);
			for(int k=0;k<KR;k++) { jk[k] = std::log((S-cs[k])/(Rr-cr[k])); jm += jk[k]/KR; }
			double jv = 0; for(int k=0;k<KR;k++) jv += (jk[k]-jm)*(jk[k]-jm);
			const double se = std::sqrt(jv*(KR-1)/KR);
			const double t = (est - ltrue)/se;
			tested++;
			if(std::abs(t) > std::abs(worst)) { worst = t; worst_s = s; }
			if(std::abs(t) > 7) { bad++; if(bad <= 5) CHECK(false, "state " << s << ": log posterior ratio sampled " << est << " exact " << ltrue << " (t=" << t << ")"); }
		}
		note("tested " + str(tested) + " states against " + ref + "; worst t=" + str(worst) + " at " + worst_s);
		CHECK(tested >= 20, "enough common states for the ratio test");
		CHECK(bad == 0, bad << " of " << tested << " posterior ratios deviate (mixture of regenerate/insert/delete/function/swap kernels)");
	}

	// ==================================================================================
	section("a kernel mixture where one kernel cannot propose from some states");
	{
		auto exact = exact_posterior(states, 1.0);
		// (a) the failing kernel returns the identity proposal: must match the posterior (validates the setup)
		auto reps_id = replicate(K, [](int){
			return run_chain_counts(MixtureHypothesis<true>(finite_grammar.generate()), finite_data, 100000);
		});
		compare_to_exact(reps_id, exact, "mixture, failure = identity proposal");
		// (b) the failing kernel returns no proposal (std::optional empty), as Fleet's proposers do
		auto reps = replicate(K, [](int){
			return run_chain_counts(MixtureHypothesis<false>(finite_grammar.generate()), finite_data, 100000);
		});
		const int nbad = compare_to_exact(reps, exact, "mixture, failure = no proposal");
		CHECK(nbad == 0, "when propose() returns no proposal, MCMCChain::run `continue`s (no sample, no MH step) and draws a NEW "
		      "proposal, so in a kernel mixture the kernels that can fail are effectively down-weighted in exactly the states where "
		      "they fail. That makes the mixture weights state-dependent and the chain stops targeting the posterior. "
		      "(Fleet's proposers -- insert/delete/swap_args/sample_function_leaving_args -- return no proposal in state-dependent "
		      "ways and are mixed this way in several Models/. A failed proposal should be treated as a rejected one: stay and yield.)");
	}

	// ==================================================================================
	section("mechanics: breakout, step counts, bookkeeping, copies");
	{
		// identical trajectories with and without the likelihood breakout (it must only skip work)
		FiniteHypothesis h0(finite_grammar.generate());
		std::vector<std::pair<std::string,double>> traj[2];
		for(int b=0;b<2;b++) {
			FleetArgs::LIKELIHOOD_BREAKOUT = (b == 0);
			DefaultRNG.seed(seed() + 99);
			MCMCChain<FiniteHypothesis> chain(h0, finite_data);
			for(auto& h : chain.run(Control(50000, 0, 1, 0))) traj[b].push_back({key(h.get_value()), h.posterior});
		}
		FleetArgs::LIKELIHOOD_BREAKOUT = true;
		CHECK(traj[0].size() == 50000 and traj[0] == traj[1], "likelihood breakout changes the trajectory");

		// every yielded sample has the right (recomputed) posterior
		MCMCChain<FiniteHypothesis> chain(h0, finite_data);
		size_t n = 0; bool post_ok = true;
		for(auto& h : chain.run(Control(20000, 0, 1, 0))) {
			n++;
			const auto& st = states.at(key(h.get_value()));
			post_ok = post_ok and close(h.prior, st.prior, 1e-12, 1e-12) and close(h.likelihood, st.loglik, 1e-12, 1e-12)
			                  and close(h.posterior, st.prior + st.loglik, 1e-12, 1e-12);
		}
		CHECK(n == 20000 and chain.samples == 20000, "Control(steps) yields exactly steps samples (got " << n << ", samples=" << chain.samples << ")");
		CHECK(post_ok, "yielded samples carry their exact prior, likelihood and posterior");
		CHECK(chain.proposals <= chain.samples and chain.acceptances <= chain.proposals and chain.acceptances > 0, "proposal/acceptance counts");
		CHECK(chain.acceptance_ratio() >= 0 and chain.acceptance_ratio() <= 1, "acceptance ratio in [0,1]");

		// copying and moving a chain
		MCMCChain<FiniteHypothesis> c2(chain);
		CHECK(c2.current == chain.current and c2.current.posterior == chain.current.posterior and c2.samples == chain.samples
		      and (double)c2.temperature == (double)chain.temperature, "MCMCChain copy");
		MCMCChain<FiniteHypothesis> c3(std::move(c2));
		CHECK(c3.current == chain.current and c3.samples == chain.samples, "MCMCChain move");
		size_t n3 = 0; for(auto& h : c3.run(Control(1000, 0, 1, 0))) { (void)h; n3++; }
		CHECK(n3 == 1000, "a moved chain still runs");

		// a hypothesis's loader pointer must follow copies/moves (else recursion / self-calls break)
		auto cur = chain.getCurrent();
		CHECK(cur.program.loader == &cur, "copied hypothesis's program loader points to the copy");
		auto mv = std::move(cur);
		CHECK(mv.program.loader == &mv, "moved hypothesis's program loader points to the new object");
	}

	return finish();
}
