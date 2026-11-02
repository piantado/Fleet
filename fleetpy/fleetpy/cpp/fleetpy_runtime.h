// fleetpy: the Python-facing runtime, written once for every model.
//
// A generated module defines its model (types, primitives, grammar, hypothesis class H)
// and then calls fp_rt::bind<H>(module, grammar, nonterminals). Everything here is generic
// over H; the generated hypothesis class provides this interface:
//
//   static constexpr bool fp_lexicon, fp_stochastic, fp_total_likelihood;
//   static H fp_sample();                                       // a draw from the prior
//   deterministic:  std::pair<bool, output_t> fp_call(const input_t&);   // (completed, value)
//   stochastic:     DiscreteDistribution<output_t> fp_dist(const input_t&);
//                   double fp_logprob(const input_t&, const output_t&);
//   lexicons:       Fleet's Lexicon interface (key_t, factors)
//
// Include after Fleet's headers.
#pragma once
#include <csignal>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

// fleetpy.Undefined, which Python-callback primitives raise; set when the module is loaded.
static nb::object* fp_undefined = nullptr;   // deliberately leaked (outlives interpreter teardown)

namespace fp_rt {

template<typename H> using row_t = std::tuple<typename H::input_t, typename H::output_t, double, double>;

template<typename H>
std::vector<typename H::datum_t> data(const std::vector<row_t<H>>& rows) {
	std::vector<typename H::datum_t> out;
	out.reserve(rows.size());
	for(auto& [i, o, r, c] : rows) out.emplace_back(i, o, r, c);
	return out;
}

inline std::string keystr(const std::string& k) { return k; }
inline std::string keystr(int k) { return std::to_string(k); }
inline std::string keystr(short k) { return std::to_string(k); }

// A hypothesis as one string: Fleet's parseable form, or for a lexicon "key \x1f parseable"
// per factor, joined by \x1e, in key order (the order of Fleet's std::map of factors).
template<typename H> std::string state(const H& h) {
	if constexpr (H::fp_lexicon) {
		std::string out;
		for(auto& [k, f] : h.factors) {
			if(not out.empty()) out += '\x1e';
			out += keystr(k) + '\x1f' + f.get_value().parseable();
		}
		return out;
	}
	else return h.get_value().parseable();
}

template<typename H> size_t nodes(const H& h) {
	if constexpr (H::fp_lexicon) {
		size_t n = 0;
		for(auto& [k, f] : h.factors) n += f.get_value().count();
		return n;
	}
	else return h.get_value().count();
}

template<typename H, typename G> double tree_logprob(G& grammar, const H& h) {
	if constexpr (H::fp_lexicon) {
		double lp = 0.0;
		for(auto& [k, f] : h.factors) lp += grammar.log_probability(f.get_value());
		return lp;
	}
	else return grammar.log_probability(h.get_value());
}

// One run at a time per model: runs set process-wide FleetArgs and the global RNG.
static std::mutex run_mutex;

inline std::unique_lock<std::mutex> lock_run() {
	std::unique_lock<std::mutex> lk(run_mutex, std::try_to_lock);
	if(not lk.owns_lock()) throw std::runtime_error("fleetpy: another run is in progress on this model");
	return lk;
}

// Ctrl-C during a run sets Fleet's CTRL_C; the previous handler is restored afterwards.
struct SigGuard {
	struct sigaction old_sa;
	SigGuard() {
		struct sigaction sa{};
		sa.sa_handler = [](int) { CTRL_C = true; };
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGINT, &sa, &old_sa);
	}
	~SigGuard() { sigaction(SIGINT, &old_sa, nullptr); }
};

// Seeding as in Fleet::initialize, plus resetting the per-thread arrival counter so that
// repeated runs in one process get the same worker-thread streams.
inline void seed(unsigned long s) {
	Rng::thread_counter = 0;
	DefaultRNG.seed(s);
}

template<typename H>
auto run(std::vector<row_t<H>> rows, int algo, size_t chains, double max_temp,
         unsigned long steps, unsigned long runtime_ms, size_t threads, unsigned long restart,
         unsigned long seed_, size_t ntop, size_t max_nodes, bool breakout) {
	auto lk = lock_run();
	auto dat = data<H>(rows);

	FleetArgs::nthreads = threads;
	FleetArgs::nchains  = chains;
	FleetArgs::steps    = steps;
	FleetArgs::runtime  = runtime_ms;
	FleetArgs::restart  = restart;
	FleetArgs::ntop     = ntop;
	FleetArgs::MAX_NODES = max_nodes;
	FleetArgs::LIKELIHOOD_BREAKOUT = breakout;
	FleetArgs::print = 0;
	FleetArgs::top_print_best = false;
	FleetArgs::MCMCYieldOnlyChanges = false;
	CTRL_C = false;
	seed(seed_);

	const auto samples0 = FleetStatistics::global_sample_count.load();
	const auto pcalls0  = FleetStatistics::posterior_calls.load();
	const auto t0 = now();

	TopN<H> top(ntop);
	std::vector<double> acceptance;
	bool interrupted = false;
	{
		nb::gil_scoped_release release;
		SigGuard sig;
		std::span<typename H::datum_t> d(dat);
		auto h0 = H::fp_sample();
		Control ctl(steps, runtime_ms, threads, restart);
		if(algo == 0) {
			MCMCChain samp(h0, d);
			for(auto& h : samp.run(ctl)) top << h;
			acceptance.push_back(samp.acceptance_ratio());
		}
		else if(algo == 1) {
			ChainPool samp(h0, d, chains);
			for(auto& h : samp.run(ctl)) top << h;
			for(auto& c : samp.pool) acceptance.push_back(c.acceptance_ratio());
		}
		else if(algo == 2) {
			ParallelTempering samp(h0, d, chains, max_temp);
			for(auto& h : samp.run(ctl)) top << h;
			for(auto& c : samp.pool) acceptance.push_back(c.acceptance_ratio());
		}
		else throw std::invalid_argument("fleetpy: unknown algorithm");
		interrupted = CTRL_C.load();
		CTRL_C = false;
	}
	const double secs = time_since(t0) / 1000.0;
	return std::make_tuple(top.sorted(false),
	                       (unsigned long)(FleetStatistics::global_sample_count.load() - samples0),
	                       (unsigned long)(FleetStatistics::posterior_calls.load() - pcalls0),
	                       secs, interrupted, acceptance);
}

// Every state of one MCMC chain: (distinct states, id sequence).
//   raw=false (the exact-posterior check): start from a prior sample with a finite posterior,
//     discard `burn` steps, and record only finite-posterior states (a chain at -inf is
//     restarting, not sampling the target).
//   raw=true (equivalence with native Fleet): exactly what a plain MCMCChain run does --
//     the first prior sample as the start, and every yielded state recorded.
template<typename H>
auto trace(std::vector<row_t<H>> rows, unsigned long steps, unsigned long burn,
           unsigned long seed_, size_t max_nodes, bool breakout, bool raw) {
	auto dat = data<H>(rows);
	auto lk = lock_run();
	FleetArgs::MAX_NODES = max_nodes;
	FleetArgs::LIKELIHOOD_BREAKOUT = breakout;
	FleetArgs::nthreads = 1;
	FleetArgs::MCMCYieldOnlyChanges = false;
	CTRL_C = false;
	seed(seed_);
	std::unordered_map<std::string, uint32_t> ids;
	std::vector<std::string> names;
	std::vector<uint32_t> seq;
	seq.reserve(steps);
	{
		nb::gil_scoped_release release;
		SigGuard sig;
		std::span<typename H::datum_t> d(dat);
		auto h0 = H::fp_sample();
		for(int tries = 0; not raw; tries++) {
			h0.compute_posterior(d);
			if(std::isfinite(h0.posterior)) break;
			if(tries > 100000) throw std::runtime_error("fleetpy.trace: no finite-posterior start found");
			h0 = H::fp_sample();
		}
		MCMCChain samp(h0, d);
		Control ctl(steps + burn, 0, 1, 0);
		unsigned long k = 0;
		for(auto& h : samp.run(ctl)) {
			if(not raw and (k++ < burn or not std::isfinite(h.posterior))) continue;
			auto [it, inserted] = ids.try_emplace(state(h), (uint32_t)names.size());
			if(inserted) names.push_back(it->first);
			seq.push_back(it->second);
		}
		CTRL_C = false;
	}
	return std::make_pair(names, seq);
}

// The Python class of hypotheses. Every method that computes works on a copy: Fleet's
// calls mutate bookkeeping fields, and the Python object must stay unchanged.
template<typename H>
void bind_hypothesis(nb::module_& m) {
	using in_t = typename H::input_t;
	using out_t = typename H::output_t;
	auto c = nb::class_<H>(m, "Hypothesis")
		.def("string", [](const H& h) { return h.string(); })
		.def("parseable", [](const H& h) { return state(h); })
		.def_prop_ro("prior", [](const H& h) { return h.prior; })
		.def_prop_ro("likelihood", [](const H& h) { return h.likelihood; })
		.def_prop_ro("posterior", [](const H& h) { return h.posterior; })
		.def_prop_ro("nodes", [](const H& h) { return nodes(h); })
		.def("compute_prior", [](const H& h) { H hc = h; return hc.compute_prior(); })
		.def("compute_posterior", [](const H& h, std::vector<row_t<H>> rows) {
			auto dat = data<H>(rows);
			H hc = h;
			std::span<typename H::datum_t> d(dat);
			hc.compute_posterior(d);
			return std::make_tuple(hc.prior, hc.likelihood, hc.posterior);
		})
		.def("__eq__", [](const H& a, const H& b) { return a == b; })
		.def("__hash__", [](const H& h) { return h.hash(); });

	if constexpr (H::fp_stochastic) {
		c.def("call", [](const H& h, const in_t& x) {
			H hc = h;
			const auto d = hc.fp_dist(x);   // a named object: values() returns a reference into it
			std::vector<std::pair<out_t, double>> out;
			for(auto& [k, v] : d.values()) out.emplace_back(k, v);
			return out;
		});
		c.def("logprob", [](const H& h, const in_t& x, const out_t& y) { H hc = h; return hc.fp_logprob(x, y); });
	}
	else {
		c.def("call", [](const H& h, const in_t& x) { H hc = h; return hc.fp_call(x); });
	}

	// The likelihood alone, without the prior: compute_posterior skips the likelihood (and
	// reports NaN) when the prior is -inf, e.g. beyond MAX_NODES.
	if constexpr (H::fp_total_likelihood) {
		c.def("total_likelihood", [](const H& h, std::vector<row_t<H>> rows) {
			auto dat = data<H>(rows);
			H hc = h;
			std::span<typename H::datum_t> d(dat);
			return hc.compute_likelihood(d);
		});
	}
	else {
		c.def("single_likelihood", [](const H& h, row_t<H> row) {
			auto dat = data<H>(std::vector<row_t<H>>{row});
			H hc = h;
			return hc.compute_single_likelihood(dat[0]);
		});
	}

	if constexpr (H::fp_lexicon) {
		using key_t = typename H::key_t;
		c.def("factors", [](const H& h) {
			std::vector<std::pair<key_t, std::string>> out;
			for(auto& [k, f] : h.factors) out.emplace_back(k, f.get_value().parseable());
			return out;
		});
		c.def("factor_strings", [](const H& h) {
			std::vector<std::pair<key_t, std::string>> out;
			for(auto& [k, f] : h.factors) out.emplace_back(k, f.string());
			return out;
		});
	}
	else {   // the pieces a Python prior can use, for its differential test
		c.def("grammar_prior", [](const H& h) { H hc = h; return hc.H::Super::compute_prior(); });
		c.def("recursion_count", [](const H& h) { H hc = h; return (int64_t)hc.recursion_count(); });
	}
}

// Everything a model module exposes, besides its own types and primitives.
template<typename H, typename G>
void bind(nb::module_& m, G& grammar, std::vector<int> nonterminals) {
	bind_hypothesis<H>(m);

	if constexpr (H::fp_lexicon) {
		using key_t = typename H::key_t;
		using factor_t = typename std::decay_t<decltype(std::declval<H&>().factors)>::mapped_type;
		m.def("from_factors", [&grammar](const std::vector<std::pair<key_t, std::string>>& fs) {
			H h;
			for(auto& [k, p] : fs) h.factors[k] = factor_t(grammar.from_parseable(p));
			return h;
		});
	}
	else {
		m.def("from_parseable", [&grammar](const std::string& s) { return H(grammar.from_parseable(s)); });
	}

	m.def("sample", [](unsigned long s) {
		if(s != 0) DefaultRNG.seed(s);
		return H::fp_sample();
	});
	m.def("nonterminals", [nonterminals]() { return nonterminals; });
	m.def("rule_formats", [&grammar]() {
		std::vector<std::pair<int, std::string>> out;
		for(auto& r : grammar) out.emplace_back((int)r.nt, r.format);
		return out;
	});
	m.def("log_probability", [&grammar](const H& h) { return tree_logprob(grammar, h); });
	m.def("set_max_nodes", [](size_t n) { FleetArgs::MAX_NODES = n; });
	m.def("run", &run<H>);
	m.def("trace", &trace<H>);
	m.def("fp_set_undefined", [](nb::object o) { fp_undefined = new nb::object(o); });
}

} // namespace fp_rt
