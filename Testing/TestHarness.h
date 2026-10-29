#pragma once

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TestHarness.h -- shared machinery for the Testing/Test*.cpp unit tests.
//
// Output contract (relied on by Testing/Makefile):
//   * stdout gets EXACTLY ONE line per test program:   <TestName> <dots> PASS|FAIL (...)
//   * everything else (sections, failing checks, statistics) goes to stderr, which the
//     Makefile redirects to Testing/logs/<TestName>.log
//   * exit code 0 iff every check passed
//
// Statistical checks: all tests are seeded (default seed 1, override with the environment
// variable FLEET_TEST_SEED). Single-threaded parts are exactly reproducible; parts that run
// replicate chains on several threads are not bit-reproducible (thread scheduling decides which
// replicate gets which per-thread RNG stream), but their verdicts are. Thresholds are set so that a
// CORRECT implementation fails with probability < ~1e-6 per check for ANY seed (so
// re-running with other seeds is a meaningful robustness check, not a lottery), while
// still being tight enough to catch real biases. Every statistical check logs the
// statistic it computed so that near-misses are visible in the log.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace TestHarness {

	inline std::string test_name = "UnnamedTest";
	inline long checks   = 0;
	inline long failures = 0;
	inline std::string current_section;

	// Remember (up to a few) failing messages so the one-line summary can name the first one
	inline std::string first_failure;

	inline void section(const std::string& s) {
		current_section = s;
		std::cerr << "\n=== " << s << " ===" << std::endl;
	}

	inline void note(const std::string& s) {
		std::cerr << "    " << s << std::endl;
	}

	inline bool record(bool ok, const std::string& msg, const char* file, int line) {
		++checks;
		if(not ok) {
			++failures;
			std::ostringstream ss;
			ss << "FAIL [" << file << ":" << line << "] (" << current_section << ") " << msg;
			std::cerr << ss.str() << std::endl;
			if(first_failure.empty()) first_failure = current_section + ": " + msg;
		}
		return ok;
	}

	/**
	 * @brief Relative/absolute closeness, treating equal infinities as close and NaN as never close.
	 */
	inline bool close(double a, double b, double rel=1e-12, double abs_=1e-12) {
		if(std::isnan(a) or std::isnan(b)) return false;
		if(std::isinf(a) or std::isinf(b)) return a == b;
		return std::abs(a-b) <= abs_ + rel*std::max(std::abs(a), std::abs(b));
	}

	/**
	 * @brief Does f throw (anything derived from std::exception, or anything at all)?
	 */
	template<typename F>
	bool throws(F f) {
		try { f(); } catch(...) { return true; }
		return false;
	}

	/**
	 * @brief Run f in a forked child and report whether the child died abnormally (assert/abort,
	 *        segfault, uncaught exception). This lets us check that documented asserts fire, and
	 *        lets a test survive a crashing bug and report it as a failure instead of dying.
	 *        Child stdout/stderr are silenced so that assert messages don't pollute the log.
	 */
	template<typename F>
	bool dies(F f) {
		std::cout.flush(); std::cerr.flush(); fflush(stdout); fflush(stderr);
		pid_t pid = fork();
		if(pid == 0) {
			if(freopen("/dev/null", "w", stdout) == nullptr) _exit(3);
			if(freopen("/dev/null", "w", stderr) == nullptr) _exit(3);
			try { f(); } catch(...) { _exit(2); }
			_exit(0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		return not (WIFEXITED(status) and WEXITSTATUS(status) == 0);
	}

	/**
	 * @brief Seed for the test, from FLEET_TEST_SEED (default 1)
	 */
	inline unsigned long seed() {
		const char* s = std::getenv("FLEET_TEST_SEED");
		if(s == nullptr or *s == 0) return 1;
		return std::strtoul(s, nullptr, 10);
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Statistics (implemented here independently of Fleet, so tests of Fleet's
	// numerics do not rely on the code under test)
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	/**
	 * @brief Regularized upper incomplete gamma Q(a,x) = Gamma(a,x)/Gamma(a), via the series for
	 *        x < a+1 and the Lentz continued fraction otherwise (Numerical Recipes 6.2).
	 *        Accurate to ~1e-14 relative in the range used here.
	 */
	inline double gamma_Q(double a, double x) {
		if(x <= 0.0) return 1.0;
		const double gln = std::lgamma(a);
		if(x < a + 1.0) {
			double ap = a, sum = 1.0/a, del = sum;
			for(int n=0; n<100000; n++) {
				ap += 1.0; del *= x/ap; sum += del;
				if(std::abs(del) < std::abs(sum)*1e-16) break;
			}
			const double P = sum * std::exp(-x + a*std::log(x) - gln);
			return 1.0 - P;
		}
		else {
			const double FPMIN = 1e-300;
			double b = x + 1.0 - a, c = 1.0/FPMIN, d = 1.0/b, h = d;
			for(int i=1; i<100000; i++) {
				const double an = -i*(i-a);
				b += 2.0;
				d = an*d + b; if(std::abs(d) < FPMIN) d = FPMIN;
				c = b + an/c; if(std::abs(c) < FPMIN) c = FPMIN;
				d = 1.0/d;
				const double del = d*c;
				h *= del;
				if(std::abs(del-1.0) < 1e-16) break;
			}
			return std::exp(-x + a*std::log(x) - gln) * h;
		}
	}

	/**
	 * @brief Upper-tail p-value of a chi-square statistic with df degrees of freedom
	 */
	inline double chisq_pvalue(double stat, double df) {
		return gamma_Q(df/2.0, stat/2.0);
	}

	/**
	 * @brief Pearson chi-square goodness of fit of observed counts against expected
	 *        probabilities (which need not be normalized -- they are normalized here).
	 *        Cells with expected count < min_expected are pooled into one cell so the chi-square
	 *        approximation is valid. Returns the p-value and logs the statistic.
	 */
	inline double chisq_test(const std::vector<double>& observed, const std::vector<double>& probs,
	                         const std::string& label, double min_expected=10.0) {
		double n = 0, z = 0;
		for(double o : observed) n += o;
		for(double p : probs) z += p;
		double stat = 0, pooled_o = 0, pooled_e = 0;
		int cells = 0;
		for(size_t i=0;i<observed.size();i++) {
			const double e = n*probs[i]/z;
			if(e < min_expected) { pooled_o += observed[i]; pooled_e += e; continue; }
			stat += (observed[i]-e)*(observed[i]-e)/e;
			cells++;
		}
		if(pooled_e > 0) {
			if(pooled_e >= min_expected) {
				stat += (pooled_o-pooled_e)*(pooled_o-pooled_e)/pooled_e;
				cells++;
			}
			else if(pooled_o > 3*min_expected + 10*pooled_e) {
				// almost no mass expected in the pooled cells but plenty observed: certainly wrong
				stat += 1e6;
			}
		}
		const double df = std::max(1, cells-1);
		const double p = chisq_pvalue(stat, df);
		std::ostringstream ss;
		ss << label << ": chi2=" << stat << " df=" << df << " p=" << p << " (n=" << n << ")";
		note(ss.str());
		return p;
	}

	/**
	 * @brief Two-sided one-sample Kolmogorov-Smirnov test of samples against a continuous CDF.
	 *        Returns the asymptotic p-value (Kolmogorov distribution). Sorts a copy of x.
	 */
	inline double ks_test(std::vector<double> x, const std::function<double(double)>& cdf, const std::string& label) {
		std::sort(x.begin(), x.end());
		const double n = x.size();
		double D = 0;
		for(size_t i=0;i<x.size();i++) {
			const double F = cdf(x[i]);
			D = std::max(D, std::max(F - i/n, (i+1)/n - F));
		}
		// Kolmogorov distribution with the Stephens (1970) small-n correction
		const double sn = std::sqrt(n);
		const double t = (sn + 0.12 + 0.11/sn) * D;
		double p = 0;
		for(int k=1;k<200;k++) {
			const double term = 2.0 * ((k%2)?1:-1) * std::exp(-2.0*k*k*t*t);
			p += term;
			if(std::abs(term) < 1e-18) break;
		}
		p = std::min(1.0, std::max(0.0, p));
		if(t < 0.2) p = 1.0; // series is not useful (and p is ~1) for tiny t
		std::ostringstream ss;
		ss << label << ": KS D=" << D << " p=" << p << " (n=" << n << ")";
		note(ss.str());
		return p;
	}

	/**
	 * @brief z-score of an observed binomial count k out of n against probability p
	 */
	inline double binomial_z(double k, double n, double p) {
		const double sd = std::sqrt(n*p*(1-p));
		if(sd == 0) return (k == n*p) ? 0.0 : INFINITY;
		return (k - n*p)/sd;
	}

	/**
	 * @brief Print the one-line summary to stdout and return the process exit code
	 */
	inline int finish() {
		std::ostringstream line;
		line << std::left << std::setw(26) << std::setfill('.') << test_name << " ";
		if(failures == 0) {
			line << "PASS (" << checks << " checks)";
		}
		else {
			// the log file is named after the source file (see Makefile), e.g. logs/TestNode.log
			std::string stem = __BASE_FILE__;
			if(auto k = stem.find_last_of('/'); k != std::string::npos) stem = stem.substr(k+1);
			if(auto k = stem.rfind(".cpp"); k != std::string::npos) stem = stem.substr(0, k);
			line << "FAIL (" << failures << " of " << checks << " checks failed; see logs/" << stem << ".log)";
		}
		std::cerr << "\n" << line.str() << std::endl;
		std::cout << line.str() << std::endl;
		return failures == 0 ? 0 : 1;
	}
}

// CHECK(cond, msg): msg may use << to stream values, e.g. CHECK(a==b, "a=" << a << " b=" << b)
#define CHECK(cond, msg) do { \
	const bool _ok = static_cast<bool>(cond); \
	std::ostringstream _ss; if(not _ok) { _ss << msg; } \
	TestHarness::record(_ok, _ss.str(), __FILE__, __LINE__); \
} while(0)

#define CHECK_CLOSE(a, b, rel, abs_, msg) do { \
	const double _a = (a), _b = (b); \
	const bool _ok = TestHarness::close(_a, _b, (rel), (abs_)); \
	std::ostringstream _ss; if(not _ok) { _ss << std::setprecision(17) << msg << "  [got " << _a << " expected " << _b << "]"; } \
	TestHarness::record(_ok, _ss.str(), __FILE__, __LINE__); \
} while(0)

// a p-value check with the suite-wide statistical threshold
#define CHECK_PVALUE(p, msg) do { \
	const double _p = (p); \
	std::ostringstream _ss; if(not (_p > 1e-6)) { _ss << msg << "  [p=" << _p << "]"; } \
	TestHarness::record(_p > 1e-6, _ss.str(), __FILE__, __LINE__); \
} while(0)

// a z-score check: |z| < 5.3 is p > ~1e-7 two-sided
#define CHECK_Z(z, msg) do { \
	const double _z = (z); \
	std::ostringstream _ss; if(not (std::abs(_z) < 5.3)) { _ss << msg << "  [z=" << _z << "]"; } \
	TestHarness::record(std::abs(_z) < 5.3, _ss.str(), __FILE__, __LINE__); \
} while(0)
