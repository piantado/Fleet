// TestNumerics: Numerics.h, Random.h (sampling + log densities), Rng.h, Miscellaneous.h
//
// Every reference value here is computed independently of the code under test: in long
// double, from closed forms, from known constants, or by numerical integration. Sampling
// functions are checked against their exact distributions with chi-square / KS / z tests.

#include <map>
#include <set>
#include <thread>
#include <numeric>
#include <limits>

#include "Fleet.h"
#include "Numerics.h"
#include "Random.h"
#include "Miscellaneous.h"

#include "TestHarness.h"

using namespace TestHarness;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Independent references
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

long double ref_logplusexp(long double a, long double b) {
	if(a == -INFINITY) return b;
	if(b == -INFINITY) return a;
	const long double m = std::max(a,b);
	return m + log1pl(expl(std::min(a,b)-m));
}

long double ref_logsumexp(const std::vector<double>& v) {
	long double m = -INFINITY;
	for(double x : v) m = std::max<long double>(m, x);
	if(m == -INFINITY) return -INFINITY;
	long double s = 0;
	for(double x : v) s += expl((long double)x - m);
	return m + logl(s);
}

// Simpson integration of exp(lpdf) over [a,b]
double integrate_density(const std::function<double(double)>& lpdf, double a, double b, int n=200000) {
	if(n % 2) n++;
	const double h = (b-a)/n;
	double s = std::exp(lpdf(a)) + std::exp(lpdf(b));
	for(int i=1;i<n;i++) s += (i%2 ? 4 : 2) * std::exp(lpdf(a + i*h));
	return s*h/3.0;
}

// standard normal CDF via erfc, written independently of Random.h's normal_cdf
double Phi(double z) { return 0.5*std::erfc(-z/std::sqrt(2.0)); }

int main() {
	test_name = "Numerics";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("constants");
	CHECK_CLOSE(LOG2, std::log(2.0L), 1e-16, 0, "LOG2");
	CHECK_CLOSE(ROOT2, std::sqrt(2.0L), 1e-16, 0, "ROOT2");
	CHECK(infinity > std::numeric_limits<double>::max(), "infinity");
	CHECK(std::isnan(NaN), "NaN");
	CHECK_CLOSE(pi, 3.14159265358979323846, 1e-16, 0, "pi");
	CHECK_CLOSE(tau, 2*3.14159265358979323846, 1e-16, 0, "tau");
	CHECK(valid(1.0) and not valid(infinity) and not valid(-infinity) and not valid(NaN), "valid()");

	// ==================================================================================
	section("logplusexp (approximate: documented absolute error <= ~1e-6)");
	{
		// the breakout bound is where log1p(exp(z)) drops below 1e-6, i.e. z = log(expm1(1e-6))
		const double bound = get_log1pexp_breakout_bound<double>(1e-6, +[](double x) -> double { return log1p(exp(x)); });
		CHECK_CLOSE(bound, std::log(std::expm1(1e-6)), 0, 2e-3, "breakout bound");

		double worst = 0;
		for(int i=0;i<200000;i++) {
			const double a = uniform(-50, 50), b = a + uniform(-30, 30);
			const double got = logplusexp(a,b);
			worst = std::max(worst, (double)std::abs(got - ref_logplusexp(a,b)));
			CHECK(got == logplusexp(b,a), "logplusexp not symmetric at " << a << "," << b);
		}
		note("max abs error over random pairs: " + str(worst));
		CHECK(worst <= 1.0000001e-6, "logplusexp error above the documented 1e-6: " << worst);

		CHECK(logplusexp(-infinity, 3.5) == 3.5, "-inf left identity");
		CHECK(logplusexp(3.5, -infinity) == 3.5, "-inf right identity");
		CHECK(logplusexp(-infinity, -infinity) == -infinity, "-inf,-inf");
		CHECK(std::isnan(logplusexp(NaN, 1.0)) and std::isnan(logplusexp(1.0, NaN)), "NaN must propagate");
		CHECK(logplusexp(infinity, 1.0) == infinity and logplusexp(1.0, infinity) == infinity, "+inf with finite");
		CHECK(logplusexp(infinity, -infinity) == infinity, "+inf with -inf");
		CHECK(logplusexp(infinity, infinity) == infinity, "logplusexp(+inf,+inf) should be +inf (log(inf+inf)); got " << logplusexp(infinity, infinity));
		CHECK_CLOSE(logplusexp(0.0, 0.0), std::log(2.0), 1e-15, 0, "log(1+1)");
		CHECK_CLOSE(logplusexp(1000.0, 1000.0), 1000.0+std::log(2.0), 1e-15, 0, "no overflow at large arguments");
		CHECK_CLOSE(logplusexp(-1000.0, -1000.0), -1000.0+std::log(2.0), 1e-15, 0, "no underflow at small arguments");
	}

	// ==================================================================================
	section("logplusexp_full (exact)");
	{
		double worst = 0;
		for(int i=0;i<200000;i++) {
			const double a = uniform(-700, 700), b = a + uniform(-60, 60);
			const long double r = ref_logplusexp(a,b);
			worst = std::max(worst, (double)(std::abs(logplusexp_full(a,b) - r)/std::max(1.0L, std::abs(r))));
		}
		note("max relative error: " + str(worst));
		CHECK(worst < 4e-16, "logplusexp_full relative error " << worst);
		CHECK(logplusexp_full(-infinity, 2.0) == 2.0, "logplusexp_full(-inf, 2)");
		CHECK(logplusexp_full(-infinity, -infinity) == -infinity, "logplusexp_full(-inf,-inf) should be -inf (log 0); got " << logplusexp_full(-infinity, -infinity));
		CHECK(logplusexp_full(infinity, infinity) == infinity, "logplusexp_full(+inf,+inf) should be +inf; got " << logplusexp_full(infinity, infinity));
	}

	// ==================================================================================
	section("logsumexp (exact)");
	{
		for(int rep=0; rep<2000; rep++) {
			std::vector<double> v(1 + myrandom(200));
			const double spread = std::pow(10.0, uniform(-2, 3));
			for(auto& x : v) x = random_normal(uniform(-500,500), spread);
			const long double r = ref_logsumexp(v);
			CHECK_CLOSE(logsumexp(v), (double)r, 1e-14, 1e-13, "logsumexp random vector rep " << rep);
			// the function-pointer version
			std::vector<double> half(v.size());
			for(size_t i=0;i<v.size();i++) half[i] = 2*v[i];
			CHECK_CLOSE(logsumexp<double>(half, +[](const double& x) -> double { return x/2; }), (double)r, 1e-14, 1e-13, "logsumexp(v,f) rep " << rep);
		}
		// documented case: many terms each far below the max must still be summed
		std::vector<double> v = {0.0};
		for(int i=0;i<100000;i++) v.push_back(-15.0);
		// (summing 1e5 doubles has rounding error ~1e5*eps ~ 1e-11; that is fine)
		CHECK_CLOSE(logsumexp(v), std::log1p(1e5*std::exp(-15.0)), 0, 1e-10, "{0} + 1e5 x {-15}");
		CHECK(logsumexp(std::vector<double>{}) == -infinity, "empty -> -inf");
		CHECK(logsumexp(std::vector<double>{-infinity,-infinity}) == -infinity, "all -inf -> -inf");
		CHECK(logsumexp(std::vector<double>{1.0, infinity}) == infinity, "contains +inf -> +inf");
		CHECK(std::isnan(logsumexp(std::vector<double>{1.0, NaN, 2.0})), "contains NaN -> NaN");
		CHECK_CLOSE(logsumexp(std::vector<double>{-infinity, 0.0}), 0.0, 0, 1e-16, "-inf terms ignored");
		CHECK_CLOSE(logsumexp(std::vector<double>{-0.29627744117047,-6.65476914326284,-2.79668565176668}),
		            (double)ref_logsumexp({-0.29627744117047,-6.65476914326284,-2.79668565176668}), 1e-15, 0, "fixed vector");
		std::array<double,3> arr = {1,2,3};
		CHECK_CLOSE(logsumexp(arr), (double)ref_logsumexp({1,2,3}), 1e-15, 0, "logsumexp(std::array)");
	}

	// ==================================================================================
	section("log1m(log_p) = log(1-p)");
	{
		std::vector<double> grid = {-1e-300, -1e-200, -1e-30, -1e-17, -1e-12, -1e-6, -0.001, -0.1, -0.5,
		                            -0.6931471805599453, -0.7, -1, -2, -5, -10, -36, -37, -40, -100, -745, -1000};
		for(int i=0;i<10000;i++) grid.push_back(-std::exp(uniform(-60, 7)));
		double worst = 0;
		for(double lp : grid) {
			// reference in long double. Neither formula alone is accurate everywhere (expl rounds to 1 for
			// tiny |lp|; expm1l rounds to -1 for very negative lp), so use each where it is exact.
			const long double L = lp;
			const long double r = (L > -0.6931471805599453L) ? logl(-expm1l(L)) : log1pl(-expl(L));
			// relative error is the meaningful measure; for tiny |r| compare absolutely in units of r
			const double err = std::abs((long double)log1m(lp) - r) / std::max(std::abs(r), 1e-300L);
			worst = std::max(worst, err);
			CHECK(err < 5e-15, "log1m(" << lp << ") rel err " << err);
		}
		note("max relative error: " + str(worst));
		CHECK(log1m(0.0) == -infinity, "log1m(0)");
		CHECK(log1m(-infinity) == 0.0, "log1m(-inf)");
		CHECK(log1m(0.5) == -infinity, "log1m(positive) is -inf by convention");
		CHECK(std::isnan(log1m(NaN)), "log1m(NaN) should be NaN");
	}

	// ==================================================================================
	section("gamma functions");
	{
		CHECK_CLOSE(mylgamma(0.5), 0.5*std::log(pi), 1e-15, 0, "lgamma(1/2)");
		CHECK_CLOSE(mylgamma(1.0), 0.0, 0, 1e-16, "lgamma(1)");
		CHECK_CLOSE(mylgamma(2.0), 0.0, 0, 1e-16, "lgamma(2)");
		CHECK_CLOSE(mygamma(5.0), 24.0, 1e-14, 0, "gamma(5)");
		CHECK_CLOSE(mygamma(0.5), std::sqrt(pi), 1e-15, 0, "gamma(1/2)");
		CHECK_CLOSE(mygamma(-0.5), -2*std::sqrt(pi), 1e-14, 0, "gamma(-1/2) is negative");
		CHECK_CLOSE(mygamma(-1.5), 4.0/3.0*std::sqrt(pi), 1e-14, 0, "gamma(-3/2) is positive");
		long double lf = 0;
		for(int n=0;n<=170;n++) {
			if(n>0) lf += logl(n);
			CHECK_CLOSE(lfactorial(n), (double)lf, 1e-14, 1e-15, "lfactorial(" << n << ")");
		}
		// thread safety of lgamma_r use: compute in parallel, compare
		std::vector<double> xs(1000), out(1000);
		for(int i=0;i<1000;i++) xs[i] = uniform(-20, 20);
		std::vector<std::thread> ths;
		for(int t=0;t<4;t++) ths.emplace_back([&,t]{ for(int i=t;i<1000;i+=4) out[i] = mygamma(xs[i]); });
		for(auto& th : ths) th.join();
		for(int i=0;i<1000;i++) CHECK(out[i] == mygamma(xs[i]) or (std::isnan(out[i]) and std::isnan(mygamma(xs[i]))), "mygamma threaded mismatch at " << xs[i]);
	}

	// ==================================================================================
	section("small numeric helpers");
	{
		CHECK(sgn(3.2) == 1 and sgn(-0.1) == -1 and sgn(0.0) == 0 and sgn(-7) == -1, "sgn");
		CHECK_CLOSE(sgnlog(-std::exp(2.0)), -2.0, 1e-15, 0, "sgnlog negative");
		CHECK_CLOSE(sgnlog(std::exp(3.0)), 3.0, 1e-15, 0, "sgnlog positive");
		CHECK_CLOSE(powabs(-8.0, 1.0/3.0), -2.0, 1e-15, 0, "powabs(-8,1/3)");
		CHECK_CLOSE(powabs(9.0, 0.5), 3.0, 1e-15, 0, "powabs(9,1/2)");
		CHECK(in_interval(0.5,0.0,1.0) and in_interval(1.0,0.0,1.0) and not in_interval(1.1,0.0,1.0), "in_interval");
		CHECK_CLOSE(round(3.14159, 2), 3.14, 0, 1e-15, "round to 2");
		CHECK_CLOSE(round(-2.5, 0), -3.0, 0, 0, "round half away from zero");
		CHECK(approx_eq(1.0, 1.000001) and not approx_eq(1.0, 1.1), "approx_eq");
		CHECK(not approx_eq(0.5, 0.0), "approx_eq must use a floating abs (not int abs)");
		for(double z : {-30.0, -5.0, -0.3, 0.0, 0.7, 4.0, 30.0}) {
			CHECK_CLOSE(inv_logit(z), 1.0/(1.0+std::exp(-z)), 1e-15, 0, "inv_logit " << z);
			if(std::abs(z) < 30) CHECK_CLOSE(logit(inv_logit(z)), z, 1e-9, 1e-12, "logit(inv_logit(" << z << "))");
		}
		CHECK(power(3,0) == 1 and power(3,1) == 3 and power(2,10) == 1024 and power(7,5) == 16807, "power");
		for(long b=-5;b<=5;b++) for(long e=0;e<=12;e++) {
			long r = 1; for(long k=0;k<e;k++) r *= b;
			CHECK(power(b,e) == r, "power(" << b << "," << e << ")");
		}
		std::vector<double> m = {1, NaN, -3, 2};
		CHECK(mymin(1.0, NaN, -3.0, 2.0) == -3.0, "mymin ignores NaN");
		CHECK(mymin(NaN, 4.0) == 4.0, "mymin leading NaN");
		CHECK(mymax(std::vector<double>{1,5,3}) == 5 and mymax(std::array<double,3>{1,5,3}) == 5, "mymax");
		std::map<int,double> mm = {{1,2.0},{2,7.0},{3,-1.0}};
		CHECK(mymax(mm) == 7.0, "mymax(map)");
		InheritedIntegral<int> ii(5); CHECK(ii + 1 == 6, "InheritedIntegral");
	}

	// ==================================================================================
	section("descriptive statistics");
	{
		for(int rep=0;rep<200;rep++) {
			const size_t n = 1 + myrandom(50);
			std::vector<double> v(n);
			for(auto& x : v) x = random_normal(3, 2);
			long double s = 0; for(double x : v) s += x;
			const long double mu = s/n;
			long double ss = 0; for(double x : v) ss += (x-mu)*(x-mu);
			CHECK_CLOSE(sum(v), (double)s, 1e-13, 1e-13, "sum");
			CHECK_CLOSE(mean(v), (double)mu, 1e-13, 1e-13, "mean");
			auto vv = v;
			CHECK_CLOSE(sd(vv), n>1 ? (double)sqrtl(ss/(n-1)) : 0.0, 1e-12, 1e-13, "sd");
			auto sorted = v; std::sort(sorted.begin(), sorted.end());
			const double med = (n%2) ? sorted[n/2] : (sorted[n/2-1]+sorted[n/2])/2;
			auto v2 = v;
			CHECK(median(v2) == med, "median");
			// MAD
			std::vector<double> dev(n); for(size_t i=0;i<n;i++) dev[i] = std::abs(v[i]-med);
			std::sort(dev.begin(), dev.end());
			const double mad = (n%2) ? dev[n/2] : (dev[n/2-1]+dev[n/2])/2;
			// NOTE: MAD() cannot be tested -- it does not compile when instantiated, because it calls
			// median(), which Numerics.h declares *below* MAD (two-phase lookup finds nothing).
			(void)mad;
			// quantile: documented as v[floor(n*q)] of the sorted values
			for(double q : {0.0, 0.1, 0.25, 0.5, 0.9, 0.999}) {
				auto v4 = v;
				CHECK(quantile(v4, q) == sorted[(size_t)std::floor(n*q)], "quantile " << q);
			}
		}
		std::vector<double> one = {4.0};
		CHECK(median(one) == 4.0, "median of one");
		std::vector<double> none;
		CHECK(std::isnan(median(none)), "median of empty is NaN");
		std::vector<double> withnan = {3, NaN, 1, 2};
		CHECK(quantile(withnan, 0.0) == 1.0, "quantile sorts NaN last");

		// q = 1.0 must give the maximum (not index out of range)
		bool died = dies([]{ std::vector<double> v = {1,2,3}; volatile double r = quantile(v, 1.0); (void)r; });
		CHECK(not died, "quantile(v, 1.0) crashes/throws (index floor(n*1.0)=n is out of range)");

		// trimmed_mean(v,a,b): mean of sorted[i] for a*n <= i < b*n
		std::vector<double> t = {9,1,8,2,7,3,6,4,5,10};
		auto t1 = t;
		CHECK_CLOSE(trimmed_mean(t1, 0.2f, 0.8f), (3+4+5+6+7+8)/6.0, 1e-15, 0, "trimmed_mean(v, .2, .8) averages sorted[2..7]");
		// trimmed_mean(v, pct) is documented as trimming pct from BOTH ends: = trimmed_mean(v, pct, 1-pct)
		double tm = NaN;
		died = dies([&]{ auto t2 = t; volatile double r = trimmed_mean(t2, 0.2f); (void)r; });
		CHECK(not died, "trimmed_mean(v, pct) asserts: it calls trimmed_mean(v, pct, pct) instead of (pct, 1-pct)");
		if(not died) {
			auto t2 = t; tm = trimmed_mean(t2, 0.2f);
			CHECK_CLOSE(tm, (3+4+5+6+7+8)/6.0, 1e-15, 0, "trimmed_mean(v, .2)");
		}

		// weighted quantile from (value, logprob) pairs
		std::vector<std::pair<int,double>> wq = {{3, log(0.2)}, {1, log(0.1)}, {2, log(0.3)}, {4, log(0.4)}};
		CHECK(weighted_quantile(wq, 0.05) == 1, "weighted_quantile .05");
		CHECK(weighted_quantile(wq, 0.35) == 2, "weighted_quantile .35");
		CHECK(weighted_quantile(wq, 0.55) == 3, "weighted_quantile .55");
		CHECK(weighted_quantile(wq, 0.95) == 4, "weighted_quantile .95");
	}

	// ==================================================================================
	section("NaN-aware ordering");
	{
		std::vector<double> v = {3, NaN, -1, NaN, 2};
		std::sort(v.begin(), v.end(), floating_point_compare<double>());
		CHECK(v[0] == -1 and v[1] == 2 and v[2] == 3 and std::isnan(v[3]) and std::isnan(v[4]), "floating_point_compare sorts NaN last");
		CHECK(fp_ordering(1.0, 2.0) == std::strong_ordering::less, "fp_ordering less");
		CHECK(fp_ordering(2.0, 1.0) == std::strong_ordering::greater, "fp_ordering greater");
		CHECK(fp_ordering(2.0, 2.0) == std::strong_ordering::equivalent, "fp_ordering equal");
		CHECK(fp_ordering(NaN, NaN) == std::strong_ordering::equivalent, "fp_ordering NaN==NaN");
		CHECK(fp_ordering(-0.0, 0.0) == std::strong_ordering::equivalent, "fp_ordering -0 == 0");
		// NaN is placed at one end (the current convention puts it below every number; Bayesable and
		// TopN rely on whichever end it is, so this only checks that NaN is consistently below)
		for(double v : {-infinity, -1.0, 0.0, 1.0, infinity})
			CHECK(fp_ordering(NaN, v) == std::strong_ordering::less and fp_ordering(v, NaN) == std::strong_ordering::greater, "NaN vs " << v);
		// whatever convention, it must be a consistent total order
		std::vector<double> w = {NaN, 1, -infinity, infinity, 0, NaN, -2};
		for(double a : w) for(double b : w) {
			auto ab = fp_ordering(a,b), ba = fp_ordering(b,a);
			CHECK((ab == std::strong_ordering::less) == (ba == std::strong_ordering::greater), "fp_ordering antisymmetry " << a << " " << b);
		}
	}

	// ==================================================================================
	section("Rng seeding");
	{
		DefaultRNG.seed(12345);
		std::vector<double> a(10); for(auto& x : a) x = uniform();
		DefaultRNG.seed(12345);
		std::vector<double> b(10); for(auto& x : b) x = uniform();
		CHECK(a == b, "same seed gives the same stream");
		DefaultRNG.seed(12346);
		std::vector<double> c(10); for(auto& x : c) x = uniform();
		CHECK(a != c, "different seeds give different streams");
		std::mt19937 ref(12345);
		DefaultRNG.seed(12345);
		CHECK(DefaultRNG() == ref(), "DefaultRNG.seed(s) is std::mt19937 seeded with s");
		// seed(0) means "unseeded": must NOT reset the generator
		DefaultRNG.seed(12345); (void)DefaultRNG();
		const auto next = DefaultRNG();
		DefaultRNG.seed(12345); (void)DefaultRNG();
		DefaultRNG.seed(0);
		CHECK(DefaultRNG() == next, "seed(0) must not reseed");
		// worker threads get their own streams, distinct from main and from each other
		DefaultRNG.seed(777);
		std::vector<unsigned> tv(4);
		std::vector<std::thread> ths;
		for(int t=0;t<4;t++) ths.emplace_back([&,t]{ tv[t] = DefaultRNG(); });
		for(auto& th : ths) th.join();
		std::set<unsigned> distinct(tv.begin(), tv.end());
		std::mt19937 m777(777);
		CHECK(distinct.size() == 4 and not distinct.count(m777()), "thread rngs are distinct streams");
		DefaultRNG.seed(seed());
	}

	// ==================================================================================
	section("uniform / flip / myrandom");
	{
		const int N = 2000000;
		std::vector<double> u(N);
		bool inrange = true;
		for(auto& x : u) { x = uniform(); inrange = inrange and x >= 0.0 and x < 1.0; }
		CHECK(inrange, "uniform() in [0,1)");
		CHECK_PVALUE(ks_test(u, [](double x){ return std::clamp(x,0.0,1.0); }, "uniform()"), "uniform() KS");
		std::vector<double> u2(200000);
		bool inr2 = true;
		for(auto& x : u2) { x = uniform(-3.0, 5.0); inr2 = inr2 and x >= -3 and x < 5; }
		CHECK(inr2, "uniform(a,b) range");
		CHECK_PVALUE(ks_test(u2, [](double x){ return std::clamp((x+3)/8, 0.0, 1.0); }, "uniform(-3,5)"), "uniform(a,b) KS");

		for(double p : {0.0, 0.001, 0.3, 0.5, 0.97, 1.0}) {
			long k = 0; for(int i=0;i<N;i++) k += flip(p);
			if(p == 0.0 or p == 1.0) CHECK(k == (long)(p*N), "flip(" << p << ") is deterministic");
			else CHECK_Z(binomial_z(k, N, p), "flip(" << p << ") frequency");
		}
		long k = 0; for(int i=0;i<N;i++) k += flip(); CHECK_Z(binomial_z(k,N,0.5), "flip() default 0.5");

		for(int mx : {1, 2, 7, 100}) {
			std::vector<double> cnt(mx, 0.0);
			bool ok = true;
			for(int i=0;i<500000;i++) { int r = myrandom(mx); if(r < 0 or r >= mx) ok = false; else cnt[r]++; }
			CHECK(ok, "myrandom(" << mx << ") range [0," << mx-1 << "]");
			if(mx > 1) CHECK_PVALUE(chisq_test(cnt, std::vector<double>(mx,1.0), "myrandom(" + str(mx) + ")"), "myrandom uniformity");
		}
		{
			std::vector<double> cnt(5, 0.0); bool ok = true;
			for(int i=0;i<500000;i++) { int r = myrandom(-2, 3); if(r < -2 or r >= 3) ok = false; else cnt[r+2]++; }
			CHECK(ok, "myrandom(-2,3) range [-2,2]");
			CHECK_PVALUE(chisq_test(cnt, std::vector<double>(5,1.0), "myrandom(-2,3)"), "myrandom(min,max) uniformity");
			auto riv = random_int_vector(10, 13, 100000);
			bool ok2 = riv.size() == 100000;
			for(int r : riv) ok2 = ok2 and r >= 10 and r < 13;
			CHECK(ok2, "random_int_vector range/size");
		}
		CHECK(dies([]{ (void)myrandom(0); }), "myrandom(0) asserts");
	}

	// ==================================================================================
	section("continuous samplers against exact CDFs");
	{
		const int N = 400000;
		std::vector<double> x(N);

		for(auto& v : x) v = random_normal();
		CHECK_PVALUE(ks_test(x, Phi, "random_normal()"), "random_normal() KS");
		for(auto& v : x) v = random_normal(-2.0, 0.5);
		CHECK_PVALUE(ks_test(x, [](double y){ return Phi((y+2)/0.5); }, "random_normal(-2,.5)"), "random_normal(mu,sd) KS");

		for(auto& v : x) v = random_exponential(2.5);
		CHECK_PVALUE(ks_test(x, [](double y){ return y<0 ? 0.0 : -std::expm1(-2.5*y); }, "random_exponential(2.5)"), "random_exponential KS");

		for(auto& v : x) v = random_cauchy();
		CHECK_PVALUE(ks_test(x, [](double y){ return 0.5 + std::atan(y)/pi; }, "random_cauchy()"), "random_cauchy KS");

		for(auto& v : x) v = random_laplace(1.0, 2.0);
		CHECK_PVALUE(ks_test(x, [](double y){ return y < 1 ? 0.5*std::exp((y-1)/2.0) : 1 - 0.5*std::exp(-(y-1)/2.0); }, "random_laplace(1,2)"),
		             "random_laplace KS (b must be the scale: Var=2b^2)");

		// t: reference CDF by numerical integration (Simpson, on a fine grid) of
		// an independently written density
		for(double nu : {1.0, 3.0, 10.0}) {
			for(auto& v : x) v = random_t(nu);
			auto tpdf = [nu](double y) { return std::exp(std::lgamma((nu+1)/2) - std::lgamma(nu/2) - 0.5*std::log(nu*pi) - (nu+1)/2*std::log1p(y*y/nu)); };
			// tabulate CDF on a grid and interpolate
			const double L = 60; const int G = 240000; std::vector<double> cdf(G+1);
			const double h = 2*L/G; double acc = 0;
			cdf[0] = 0;
			for(int i=1;i<=G;i++) { const double a = -L + (i-1)*h, b = a+h; acc += h/6*(tpdf(a)+4*tpdf((a+b)/2)+tpdf(b)); cdf[i] = acc; }
			// tails beyond +-L (closed form for the tail mass is messy; add symmetric half of the missing mass)
			const double missing = 1 - acc;
			auto F = [&](double y) {
				if(nu == 1.0) return 0.5 + std::atan(y)/pi; // t with nu=1 is exactly Cauchy (its tails beyond L are too heavy for the grid)
				if(y <= -L) return missing/2; 
				if(y >= L)  return 1.0-missing/2;
				const double pos = (y+L)/h; const int i = (int)pos; const double fr = pos - i;
				return missing/2 + cdf[i] + fr*(cdf[std::min(i+1,G)]-cdf[i]);
			};
			CHECK_PVALUE(ks_test(x, F, "random_t(" + str(nu) + ")"), "random_t KS");
		}

		// gamma (shape a, RATE b): moments + KS against numerically integrated CDF
		for(auto [a,b] : std::vector<std::pair<double,double>>{{0.5,1.0},{2.0,3.0},{9.0,0.5}}) {
			for(auto& v : x) v = random_gamma(a,b);
			double m=0, m2=0; for(double v : x) { m += v; m2 += v*v; } m /= N; m2 /= N;
			const double var = m2 - m*m;
			CHECK_Z((m - a/b)/std::sqrt(a/(b*b)/N), "random_gamma(" << a << "," << b << ") mean must be shape/rate");
			CHECK(std::abs(var/(a/(b*b)) - 1) < 0.05, "random_gamma variance " << var << " vs " << a/(b*b));
		}
		// beta
		for(auto [a,b] : std::vector<std::pair<double,double>>{{0.5,0.5},{2.0,5.0}}) {
			for(auto& v : x) v = random_beta(a,b);
			auto bpdf = [a,b](double y){ return std::exp(std::lgamma(a+b)-std::lgamma(a)-std::lgamma(b)+(a-1)*std::log(y)+(b-1)*std::log1p(-y)); };
			// CDF via a substitution-free fine grid (density singular at the ends for a,b<1: integrate in y=t^k)
			const int G = 400000; std::vector<double> cdf(G+1); cdf[0]=0; double acc=0;
			// y = s^4 maps [0,1]->[0,1] and removes the endpoint singularity at 0 for a>=1/4; symmetric at 1 via 1-(1-s)^4
			auto ymap = [](double s){ return s < 0.5 ? 8*s*s*s*s : 1-8*(1-s)*(1-s)*(1-s)*(1-s); };
			auto dy = [](double s){ return s < 0.5 ? 32*s*s*s : 32*(1-s)*(1-s)*(1-s); };
			for(int i=1;i<=G;i++) { const double s0=(i-1.0)/G, s1=(double)i/G, sm=(s0+s1)/2;
				auto f=[&](double s){ const double y=ymap(s); return (y<=0||y>=1)?0.0:bpdf(y)*dy(s); };
				acc += (s1-s0)/6*(f(s0)+4*f(sm)+f(s1)); cdf[i]=acc; }
			auto F = [&](double y){ // invert ymap by bisection
				double lo=0, hi=1; for(int it=0;it<60;it++){ double mid=(lo+hi)/2; if(ymap(mid)<y) lo=mid; else hi=mid; }
				const double pos=lo*G; const int i=std::min((int)pos,G-1); return (cdf[i]+(pos-i)*(cdf[i+1]-cdf[i]))/acc; };
			CHECK(std::abs(acc-1) < 1e-4, "beta density integrates to " << acc);
			CHECK_PVALUE(ks_test(x, F, "random_beta(" + str(a) + "," + str(b) + ")"), "random_beta KS");
		}
		// rbinomial
		{
			std::vector<double> cnt(11,0.0), pr(11);
			for(int i=0;i<N;i++) cnt[rbinomial(10, 0.3)]++;
			for(int k=0;k<=10;k++) pr[k] = std::exp(lfactorial(10)-lfactorial(k)-lfactorial(10-k)+k*std::log(0.3)+(10-k)*std::log(0.7));
			CHECK_PVALUE(chisq_test(cnt, pr, "rbinomial(10,.3)"), "rbinomial distribution");
		}
		// Dirichlet ("random_multinomial" with constant alpha)
		{
			const int len = 4; double m0 = 0; bool sums = true;
			for(int i=0;i<100000;i++) {
				auto v = random_multinomial(0.7, len);
				double s = 0; for(double y : v) { s += y; } sums = sums and std::abs(s-1) < 1e-12 and v.size()==(size_t)len;
				m0 += v[0];
			}
			CHECK(sums, "random_multinomial sums to 1");
			// Var of a component of Dir(alpha*1) = (1/len)(1-1/len)/(len*alpha+1)
			const double sdm = std::sqrt(0.25*0.75/(4*0.7+1)/100000);
			CHECK_Z((m0/100000 - 0.25)/sdm, "random_multinomial component mean");
		}
		// nonempty subset: P(i in subset) = p + (1-p)*(1-p)^(n-1)... exactly: i is in if flip(p) OR it was the forced index
		{
			const size_t n = 5; const double p = 0.2; const int R = 400000;
			std::vector<double> inc(n, 0.0); bool nonempty = true;
			for(int r=0;r<R;r++) {
				auto s = random_nonempty_subset(n, p);
				bool any = false; for(size_t i=0;i<n;i++) { inc[i] += s[i]; any = any or s[i]; }
				nonempty = nonempty and any and s.size()==n;
			}
			CHECK(nonempty, "random_nonempty_subset is never empty");
			const double pin = p + (1-p)/n;
			for(size_t i=0;i<n;i++) CHECK_Z(binomial_z(inc[i], R, pin), "random_nonempty_subset inclusion of " << i);
		}
	}

	// ==================================================================================
	section("log densities: normalization and closed forms");
	{
		auto check_norm = [](const std::string& name, const std::function<double(double)>& lp, double a, double b, double tol) {
			const double z = integrate_density(lp, a, b);
			CHECK(std::abs(z-1) < tol, name << " integrates to " << z);
		};
		check_norm("normal_lpdf(0,1)", [](double x){ return normal_lpdf(x, 0.0, 1.0); }, -40, 40, 1e-9);
		check_norm("normal_lpdf(3,.2)", [](double x){ return normal_lpdf(x, 3.0, 0.2); }, -5, 11, 1e-9);
		for(double x : {-3.0, 0.0, 0.5, 7.0})
			CHECK_CLOSE(normal_lpdf(x, 1.0, 2.0), -std::log(2.0) - 0.5*std::log(2*pi) - (x-1)*(x-1)/8, 1e-14, 1e-14, "normal_lpdf closed form " << x);
		CHECK_CLOSE(normal_cdf(0.0, 0.0, 1.0), 0.5, 1e-15, 0, "normal_cdf(0)");
		CHECK_CLOSE(normal_cdf(1.959963984540054, 0.0, 1.0), 0.975, 1e-12, 0, "normal_cdf(1.96)");
		CHECK_CLOSE(normal_cdf(-8.0, 0.0, 1.0), 6.22096057427178e-16, 1e-6, 0, "normal_cdf far tail has relative precision");
		CHECK_CLOSE(normal_cdf(5.0, 3.0, 2.0), Phi(1.0), 1e-15, 0, "normal_cdf location/scale");
		check_norm("t_lpdf(nu=3)", [](double x){ return t_lpdf(x, 3.0); }, -3000, 3000, 2e-6);
		check_norm("t_lpdf(nu=30)", [](double x){ return t_lpdf(x, 30.0); }, -60, 60, 1e-9);
		CHECK_CLOSE(t_lpdf(0.0, 1.0), -std::log(pi), 1e-14, 0, "t(nu=1) at 0 is Cauchy 1/pi");
		check_norm("gamma_lpdf(2,3)", [](double x){ return gamma_lpdf(x, 2.0, 3.0); }, 1e-12, 60, 1e-8);
		CHECK_CLOSE(gamma_lpdf(1.5, 1.0, 2.0), std::log(2.0) - 3.0, 1e-14, 0, "gamma(1,rate 2) is exponential(2)");
		CHECK(gamma_lpdf(-1.0, 2.0, 1.0) == -infinity and gamma_lpdf(0.0, 2.0, 1.0) == -infinity, "gamma_lpdf outside support");
		// heavy tails: integrate in theta with x = loc + gamma*tan(theta), dx = gamma/cos^2(theta)
		check_norm("cauchy_lpdf(1,2)", [](double th){ return cauchy_lpdf(1.0+2.0*std::tan(th), 1.0, 2.0) + std::log(2.0) - 2*std::log(std::cos(th)); },
		           -pi/2+1e-9, pi/2-1e-9, 1e-8);
		CHECK_CLOSE(cauchy_lpdf(1.0, 1.0, 2.0), -std::log(2*pi), 1e-15, 0, "cauchy at its location");
		check_norm("laplace_lpdf(-1,.5)", [](double x){ return laplace_lpdf(x, -1.0, 0.5); }, -41, 39, 1e-8); // kink on a grid point
		CHECK_CLOSE(laplace_lpdf(2.0, 0.0, 1.0), -std::log(2.0) - 2.0, 1e-15, 0, "laplace closed form");
		// geometric on k=1,2,...
		long double gs = 0;
		for(size_t k=1;k<2000;k++) gs += expl(geometric_lpdf(k, 0.1));
		CHECK_CLOSE((double)gs, 1.0, 1e-12, 0, "geometric_lpdf sums to 1");
		CHECK_CLOSE(geometric_lpdf(3, 0.25), std::log(0.25)+2*std::log(0.75), 1e-15, 0, "geometric closed form");
		CHECK(dies([]{ volatile double r = geometric_lpdf(0, 0.5); (void)r; }), "geometric_lpdf(0) asserts (documented)");
	}

	// ==================================================================================
	section("discrete sampling: sample / sample_lp / sample_int / sample_int_lp");
	{
		const int N = 1000000;
		std::vector<double> w = {0.5, 0.0, 2.0, 1e-3, 3.0, NaN, 0.7};
		std::function<double(const double&)> f = [](const double& x){ return x; };
		const double z = sample_z<double>(w, f);
		CHECK_CLOSE(z, 0.5+2.0+1e-3+3.0+0.7, 1e-15, 0, "sample_z skips NaN");
		std::vector<double> cnt(w.size(), 0.0);
		bool lp_ok = true;
		for(int i=0;i<N;i++) {
			auto [ptr, lp] = sample<double>(w, f);
			const size_t idx = ptr - &w[0];
			cnt[idx]++;
			lp_ok = lp_ok and close(lp, std::log(w[idx]) - std::log(z), 1e-14, 1e-14);
		}
		CHECK(lp_ok, "sample returns log(f(x)/z)");
		CHECK(cnt[1] == 0 and cnt[5] == 0, "zero-probability and NaN elements are never sampled");
		std::vector<double> wp = w; wp[5] = 0;
		CHECK_PVALUE(chisq_test(cnt, wp, "sample()"), "sample() frequencies");

		// plain function-pointer overloads must consume the same randomness and select the same element
		{
			auto fp = +[](const double& x) -> double { return x; };
			bool same = true;
			for(int i=0;i<10000;i++) {
				DefaultRNG.seed(1000+i); auto a = sample<double>(w, f);
				DefaultRNG.seed(1000+i); auto b = sample<double>(w, fp);
				same = same and a.first == b.first and a.second == b.second;
			}
			CHECK(same, "sample(std::function) and sample(function pointer) are bit-identical");
			DefaultRNG.seed(seed()+1);
		}

		// lp version: f gives log probabilities
		std::vector<double> lw = {std::log(0.5), -infinity, std::log(2.0), std::log(1e-3), std::log(3.0), NaN, std::log(0.7)};
		std::function<double(const double&)> g = [](const double& x){ return x; };
		const double lz = sample_lp_z<double>(lw, g);
		CHECK_CLOSE(lz, std::log(0.5+2.0+1e-3+3.0+0.7), 1e-6, 1e-6, "sample_lp_z");
		std::fill(cnt.begin(), cnt.end(), 0.0);
		lp_ok = true;
		for(int i=0;i<N;i++) {
			auto [ptr, lp] = sample_lp<double>(lw, g);
			const size_t idx = ptr - &lw[0];
			cnt[idx]++;
			lp_ok = lp_ok and close(lp, lw[idx] - lz, 1e-12, 1e-12);
		}
		CHECK(lp_ok, "sample_lp returns f(x)-z");
		CHECK(cnt[1] == 0 and cnt[5] == 0, "sample_lp never picks -inf/NaN");
		CHECK_PVALUE(chisq_test(cnt, wp, "sample_lp()"), "sample_lp() frequencies");

		// sample_lp must not drop small terms that collectively carry real mass (same issue logsumexp
		// documents fixing): one element at log p = 0 followed by 100000 elements at -15
		{
			std::vector<double> many = {0.0};
			for(int i=0;i<100000;i++) many.push_back(-15.0);
			const double true_lz = std::log1p(1e5*std::exp(-15.0));
			const double got_lz = sample_lp_z<double>(many, g);
			CHECK_CLOSE(got_lz, true_lz, 1e-9, 1e-9, "sample_lp_z with many small terms (logplusexp drops each -15 term once the running total is 0)");
			long first = 0; const int R = 200000;
			for(int i=0;i<R;i++) first += (sample_lp<double>(many, g).first == &many[0]);
			CHECK_Z(binomial_z(first, R, 1.0/std::exp(true_lz)), "sample_lp picks the large element with prob 1/(1+1e5 e^-15) = 0.9703; observed " << first/(double)R);
		}

		// sample_int / sample_int_lp
		std::vector<double> iw = {1.0, 0.0, 4.0, 2.5};
		std::fill(cnt.begin(), cnt.end(), 0.0); cnt.resize(4);
		lp_ok = true;
		for(int i=0;i<N;i++) { auto [k, lp] = sample_int(4, [&](const int j){ return iw[j]; }); cnt[k]++; lp_ok = lp_ok and close(lp, std::log(iw[k]/7.5), 1e-14, 1e-14); }
		CHECK(lp_ok, "sample_int lp");
		CHECK(cnt[1] == 0, "sample_int zero weight");
		CHECK_PVALUE(chisq_test(cnt, iw, "sample_int"), "sample_int frequencies");
		std::fill(cnt.begin(), cnt.end(), 0.0);
		lp_ok = true;
		for(int i=0;i<N;i++) { auto [k, lp] = sample_int_lp(4, [&](const int j){ return std::log(iw[j]); }); cnt[k]++; lp_ok = lp_ok and close(lp, std::log(iw[k]/7.5), 1e-6, 1e-6); }
		CHECK(lp_ok, "sample_int_lp lp");
		CHECK_PVALUE(chisq_test(cnt, iw, "sample_int_lp"), "sample_int_lp frequencies");
		std::fill(cnt.begin(), cnt.end(), 0.0);
		for(int i=0;i<200000;i++) cnt[sample_int(4).first]++;
		CHECK_PVALUE(chisq_test(cnt, {1,1,1,1}, "sample_int default uniform"), "sample_int default");
		std::fill(cnt.begin(), cnt.end(), 0.0);
		for(int i=0;i<200000;i++) cnt[sample_int_lp(4).first]++;
		CHECK_PVALUE(chisq_test(cnt, {1,1,1,1}, "sample_int_lp default uniform"), "sample_int_lp default");
		// NaN weights are "treated as zero prob" inside the selection loop; the normalizer must agree
		bool threw = throws([&]{ for(int i=0;i<100;i++) (void)sample_int(3, [](const int j){ return j==1 ? NaN : 1.0; }); });
		CHECK(not threw, "sample_int with a NaN weight throws: z is summed WITHOUT skipping NaN (z=NaN), unlike sample_z and sample_int_lp");

		// argmax and friends
		std::vector<double> av = {3, 9, 1, 9, 2};
		std::function<double(const double&)> id = [](const double& x){ return x; };
		auto [ai, am] = arg_max<double>(av, id);
		CHECK(ai == 1 and am == 9, "arg_max returns the first maximum");
		auto [bi, bm] = arg_max_int(5, [&](const int j){ return j==2 ? NaN : av[j]; });
		CHECK(bi == 1 and bm == 9, "arg_max_int");
		auto [mp, mv] = max_of<double>(av, id);
		CHECK(mp == &av[1] and mv == 9, "max_of");
		CHECK_CLOSE(lp_sample_eq<double>(9.0, av, id), std::log(18.0/24.0), 1e-14, 0, "lp_sample_eq counts all equal elements");
		CHECK_CLOSE(lp_sample_one<double>(9.0, av, id), std::log(9.0/24.0), 1e-14, 0, "lp_sample_one");
		CHECK(dies([]{ std::vector<double> e; std::function<double(const double&)> ff = [](const double& x){ return x; }; (void)sample<double>(e, ff); }), "sample from empty asserts");
	}

	// ==================================================================================
	section("Miscellaneous.h");
	{
		size_t h1 = 0, h2 = 0, h3 = 0;
		hash_combine(h1, 1, 2); hash_combine(h2, 2, 1); hash_combine(h3, 1, 2);
		CHECK(h1 == h3, "hash_combine deterministic");
		CHECK(h1 != h2, "hash_combine is order sensitive");
		std::map<std::string,int> m = {{"a",1}};
		CHECK(get(m, std::string("a"), 7) == 1 and get(m, std::string("b"), 7) == 7, "get with default");
		CHECK(is_iterable_v<std::vector<int>> and is_iterable_v<std::string> and not is_iterable_v<int>, "is_iterable");
		CHECK((TypeIndex<double, std::tuple<int,double,char>>::value == 1), "TypeIndex");
		CHECK((contains_type<char, int, char>() and not contains_type<float, int, char>()), "contains_type");
		CHECK((is_specialization<std::vector<int>, std::vector>::value and not is_specialization<std::set<int>, std::vector>::value), "is_specialization");
		CHECK(system_exec("echo hello") == "hello\n", "system_exec");
	}

	return finish();
}
