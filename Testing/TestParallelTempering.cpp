// TestParallelTempering: ParallelTempering's ladder, swap rule, adaptation, and sampling.
//
//  1. Ladder construction (geometric, endpoints exactly 1 and maxT; explicit list).
//  2. lp_swap against an independent formula (only the LIKELIHOOD is tempered), and swap()
//     exchanging hypotheses (and steps_since_improvement) but not temperatures.
//  3. adapt(): reproduces the arXiv:1501.05823 log-spacing update (rescaled to the fixed
//     [1, T_max] span), computed independently; keeps both endpoints fixed and the ladder increasing.
//  4. A deterministic, single-threaded emulation that drives the real pool chains, lp_swap and
//     swap: the T=1 chain must sample the exact posterior and the hottest chain must sample the
//     exact tempered posterior (replicate chains vs a 309-program enumeration).
//  5. The real multi-threaded run(): the T=1 (chain 0) samples match the exact posterior, swaps
//     actually happen, every sample carries its exact posterior, endpoints stay fixed,
//     and FleetArgs::yieldOnlyChainOne yields only chain 0.

#include "TestModels.h"
#include "ParallelTempering.h"

using namespace TestHarness;

using H = FiniteHypothesis;

int main() {
	test_name = "ParallelTempering";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());
	auto states = finite_states();

	// ==================================================================================
	section("ladder construction");
	{
		H h0(finite_grammar.generate());
		for(unsigned long n : {1ul, 2ul, 5ul, 8ul}) {
			ParallelTempering<H> pt(h0, finite_data, n, 37.5);
			CHECK(pt.nchains() == n and pt.swap_history.size() == n, "n chains " << n);
			CHECK(pt.pool[0].temperature == 1.0, "T[0] must be exactly 1 (n=" << n << ")");
			if(n > 1) CHECK_CLOSE((double)pt.pool[n-1].temperature, 37.5, 1e-14, 0, "T[n-1] = maxT (n=" << n << ")");
			for(size_t i=1;i<n;i++) {
				CHECK_CLOSE((double)pt.pool[i].temperature, std::pow(37.5, (double)i/(n-1)), 1e-14, 0, "geometric ladder T[" << i << "] (n=" << n << ")");
				CHECK(pt.pool[i].temperature > pt.pool[i-1].temperature, "ladder increasing");
			}
			CHECK(pt.pool[0].current == h0, "chain 0 starts at h0");
			bool post_ok = true;
			for(auto& c : pt.pool) {
				const auto& st = states.at(key(c.current.get_value()));
				post_ok = post_ok and close(c.current.posterior, st.prior + st.loglik, 1e-12, 1e-12);
			}
			CHECK(post_ok, "every chain's starting hypothesis has its posterior computed");
		}
		ParallelTempering<H> pt(h0, finite_data, {1.0, 2.0, 9.0});
		CHECK(pt.nchains() == 3 and pt.pool[1].temperature == 2.0 and pt.pool[2].temperature == 9.0, "explicit temperature list");
	}

	// ==================================================================================
	section("lp_swap and swap");
	{
		H h0(finite_grammar.generate());
		ParallelTempering<H> pt(h0, finite_data, 4, 10.0);
		for(int r=0;r<200;r++) {
			for(auto& c : pt.pool) { c.current = H(finite_grammar.generate()); c.current.compute_posterior(finite_data); }
			const size_t i = myrandom(4), j = myrandom(4);
			const double Ti = pt.pool[i].temperature, Tj = pt.pool[j].temperature;
			const auto& a = pt.pool[i].current; const auto& b = pt.pool[j].current;
			// joint target prod_k prior(x_k) L(x_k)^(1/T_k); swapping x_i and x_j changes it by:
			const double expect = (a.likelihood/Tj + b.likelihood/Ti) - (a.likelihood/Ti + b.likelihood/Tj);
			CHECK_CLOSE(pt.lp_swap(i,j), expect, 1e-12, 1e-12, "lp_swap(" << i << "," << j << ")");
			CHECK(pt.lp_swap(i,j) == pt.lp_swap(j,i), "lp_swap symmetric");
		}
		const std::string s0 = key(pt.pool[0].current.get_value()), s1 = key(pt.pool[1].current.get_value());
		pt.pool[0].steps_since_improvement = 11; pt.pool[1].steps_since_improvement = 22;
		pt.swap(0, 1);
		CHECK(key(pt.pool[0].current.get_value()) == s1 and key(pt.pool[1].current.get_value()) == s0, "swap exchanges hypotheses");
		CHECK(pt.pool[0].steps_since_improvement == 22 and pt.pool[1].steps_since_improvement == 11, "swap exchanges steps_since_improvement");
		CHECK(pt.pool[0].temperature == 1.0 and pt.pool[1].temperature > 1.0, "swap leaves temperatures in place");
		CHECK(pt.pool[0].current.program.loader == &pt.pool[0].current, "swapped hypothesis's program loader points to its new home");
	}

	// ==================================================================================
	section("adapt()");
	{
		H h0(finite_grammar.generate());
		for(int r=0;r<50;r++) {
			const size_t n = 3 + myrandom(6);
			ParallelTempering<H> pt(h0, finite_data, n, 50.0);
			std::vector<double> rate(n);
			for(size_t i=1;i<n;i++) {
				rate[i] = uniform();
				pt.swap_history[i].reset();
				for(int k=0;k<100;k++) pt.swap_history[i] << flip(rate[i]);
				pt.pool[i].samples = myrandom(2000000);
			}
			std::vector<double> T(n); for(size_t i=0;i<n;i++) T[i] = pt.pool[i].temperature;
			std::vector<double> m(n); for(size_t i=1;i<n;i++) m[i] = pt.swap_history[i].mean();
			// independent computation of one adaptation step (v=3, t0=1e6): the interior log spacings move by
			// k*(m_i - m_{i+1}), the top spacing is unchanged, then all spacings are scaled to span [1, T_max]
			std::vector<double> d(n, 0.0); double total = 0;
			for(size_t i=1;i<n;i++) {
				double S = std::log(T[i] - T[i-1]);
				if(i+1 < n) S += (1.0/3) * 1e6/(pt.pool[i].samples + 1e6) * (m[i] - m[i+1]);
				d[i] = std::exp(S); total += d[i];
			}
			std::vector<double> newT = T;
			for(size_t i=1;i+1<n;i++) newT[i] = newT[i-1] + d[i]*(T[n-1]-T[0])/total;
			pt.adapt();
			for(size_t i=0;i<n;i++) CHECK_CLOSE((double)pt.pool[i].temperature, newT[i], 1e-12, 1e-12, "adapt T[" << i << "] of " << n);
			CHECK(pt.pool[0].temperature == 1.0 and std::abs(pt.pool[n-1].temperature - 50.0) < 1e-12, "adapt keeps both endpoints fixed");
		}
		// repeated adaptation in a consistent direction must not invert the ladder
		ParallelTempering<H> pt(h0, finite_data, 5, 10.0);
		for(size_t i=1;i<5;i++) { pt.swap_history[i].reset(); for(int k=0;k<100;k++) pt.swap_history[i] << (i < 4); }
		bool increasing = true; std::string ladder;
		for(int a=0;a<30;a++) {
			pt.adapt();
			for(size_t i=1;i<5;i++) increasing = increasing and pt.pool[i].temperature > pt.pool[i-1].temperature;
		}
		for(auto& c : pt.pool) ladder += str((double)c.temperature) + " ";
		CHECK(increasing, "after repeated adaptation the ladder is no longer increasing (interior temperatures can grow past the fixed "
		      "maximum because each spacing is updated independently): " << ladder);
	}

	// ==================================================================================
	section("deterministic emulation: real chains + lp_swap + swap, exact tempered posteriors");
	{
		const std::vector<double> temps = {1.0, 2.5, 6.0, 15.0};
		auto exact0 = exact_posterior(states, temps[0]);
		auto exact3 = exact_posterior(states, temps[3]);
		std::vector<Counts> c0, c3;
		std::mutex mtx;
		const int K = 32;
		auto reps = replicate(K, [&](int){
			H h0(finite_grammar.generate());
			ParallelTempering<H> pt(h0, finite_data, {temps[0], temps[1], temps[2], temps[3]});
			Counts a, b;
			for(int round=0; round<3000; round++) {
				for(size_t i=0;i<pt.nchains();i++) {
					for(auto& h : pt.pool[i].run(Control(20, 0, 1, 0))) {
						if(i == 0) a[key(h.get_value())]++;
						if(i == 3) b[key(h.get_value())]++;
					}
				}
				for(size_t k=pt.nchains()-1;k>=1;k--) {
					const double R = pt.lp_swap(k, k-1);
					if(valid(R) and (R >= 0 or flip(std::exp(R)))) pt.swap(k, k-1);
				}
			}
			std::lock_guard g(mtx); c3.push_back(b);
			return a;
		});
		compare_to_exact(reps, exact0, "emulated PT, T=1 chain");
		compare_to_exact(c3, exact3, "emulated PT, T=15 chain");
	}

	// ==================================================================================
	section("multi-threaded ParallelTempering::run");
	{
		// The output queue ring is sized from FleetArgs::nthreads when the sampler is CONSTRUCTED,
		// but run(ctl) launches ctl.nthreads threads.
		FleetArgs::nthreads = 1;
		CHECK(not dies([]{
			H h(finite_grammar.generate());
			ParallelTempering<H> pt(h, finite_data, 3, 5.0);
			for(auto& x : pt.run(Control(3000, 0, 2, 0))) { (void)x; }
		}), "running with Control(nthreads=2) while FleetArgs::nthreads=1 crashes: ThreadedInferenceInterface sizes its "
		    "ConcurrentQueueRing from FleetArgs::nthreads at construction, and thread 1 then pushes out of range");
		FleetArgs::nthreads = 2; // what Fleet::initialize would have set for --threads=2

		auto exact0 = exact_posterior(states, 1.0);
		const int K = 24;
		std::atomic<size_t> total_swaps_tried{0}, total_swaps_done{0}, bad_post{0}, nonzero_idx{0}, bad_ends{0};
		auto reps = replicate(K, [&](int){
			H h0(finite_grammar.generate());
			ParallelTempering<H> pt(h0, finite_data, 4, 20.0);
			pt.swap_every = 1; pt.adapt_every = 200; pt.steps_before_change = 50;
			Counts a;
			for(auto& h : pt.run(Control(400000, 0, 2, 0))) {
				const auto& st = states.at(key(h.get_value()));
				if(not close(h.posterior, st.prior + st.loglik, 1e-12, 1e-12)) bad_post++;
				if(h.born_chain_idx == 0) a[key(h.get_value())]++;
				else nonzero_idx++;
			}
			for(size_t i=1;i<pt.nchains();i++) {
				total_swaps_tried += pt.swap_history[i].N;
				total_swaps_done  += (size_t)std::round(pt.swap_history[i].mean() * std::min<size_t>(pt.swap_history[i].N, 100));
			}
			if(pt.pool[0].temperature != 1.0 or std::abs(pt.pool[3].temperature - 20.0) > 1e-9) bad_ends++;
			return a;
		}, 1); // one PT at a time: each already runs 2 worker threads plus swapper/adapter threads
		note("swap attempts " + str((size_t)total_swaps_tried) + ", accepted (recent) " + str((size_t)total_swaps_done));
		CHECK(total_swaps_tried > 1000 and total_swaps_done > 0, "swaps were attempted and accepted");
		CHECK(bad_post == 0, bad_post << " yielded samples had a posterior inconsistent with their program");
		CHECK(nonzero_idx > 0, "run() yields samples from all chains by default");
		CHECK(bad_ends == 0, "T[0]=1 and T[max] stay fixed during run()");
		compare_to_exact(reps, exact0, "threaded PT, chain 0");

		FleetArgs::yieldOnlyChainOne = true;
		H h0(finite_grammar.generate());
		ParallelTempering<H> pt(h0, finite_data, 3, 5.0);
		size_t other = 0, n = 0;
		for(auto& h : pt.run(Control(30000, 0, 2, 0))) { n++; other += (h.born_chain_idx != 0); }
		FleetArgs::yieldOnlyChainOne = false;
		CHECK(n > 0 and other == 0, "yieldOnlyChainOne yields only chain 0 (" << other << " of " << n << " were not)");
	}

	return finish();
}
