/* Unit test for p_delete_append (src/Strings.h).
 *
 * The model: x -> y by deleting from the END of x (each further deletion w.p. del_p, forced
 * stop at the empty string), then appending (each further append w.p. add_p, characters
 * uniform on an alphabet of size A). Only a shared prefix of x and y survives, so
 *
 *    p(y|x) = sum_{k=0..M} del_p^(|x|-k) * (1-del_p)^[k>0] * (add_p/A)^(|y|-k) * (1-add_p)
 *
 * with M = longest common prefix length. p_delete_append evaluates this with an O(1)
 * closed-form geometric series; this test checks it three ways:
 *
 *  (1) Monte Carlo: simulate the generative process directly and compare outcome
 *      frequencies to exp(p_delete_append) (validates the MODEL semantics end to end);
 *  (2) exact reference: the literal k-sum evaluated term by term in long double
 *      (validates the closed form, including empty strings and no-common-prefix cases);
 *  (3) p_delete_append is generic over the sequence type: a std::vector<int> holding the
 *      same content as a std::string must give the identical answer.
 */

#include <string>
#include <vector>
#include <cmath>
#include <random>

#include "Fleet.h"
#include "Strings.h"

static const float AP_mc = 0.35f, DP_mc = 0.45f;  // big enough for Monte-Carlo coverage
static const float AP1 = 0.01f,  DP1 = 0.01f;     // FLT's strgamma
static const float AP2 = 0.20f,  DP2 = 0.50f;
static const float AP3 = 0.70f,  DP3 = 0.95f;
static const float AP4 = 0.99f,  DP4 = 0.05f;

// exact reference: the literal sum, term by term, in long double
template<const float& add_p, const float& del_p>
double reference(const std::string& x, const std::string& y, const double log_alphabet) {
	const long double la  = logl((long double)add_p),      ld  = logl((long double)del_p);
	const long double l1a = logl(1.0L-(long double)add_p), l1d = logl(1.0L-(long double)del_p);
	long double tot = expl(ld*(long double)x.size() + (la-(long double)log_alphabet)*(long double)y.size() + l1a);
	for(size_t k=1; k<=std::min(x.size(),y.size()); k++){
		if(x[k-1] != y[k-1]) break;
		tot += expl(ld*(long double)(x.size()-k) + l1d + (la-(long double)log_alphabet)*(long double)(y.size()-k) + l1a);
	}
	return (double)logl(tot);
}

size_t nchecks = 0, nfail = 0;
void expect(bool ok, const std::string& what) {
	nchecks++;
	if(not ok) { nfail++; print("FAIL:", what); }
}

template<const float& ap, const float& dp>
void check_regime(std::mt19937_64& gen, int ntrials) {
	for(int trial=0; trial<ntrials; trial++){
		const size_t A = 1 + gen()%26;
		const float lA = log((float)A);
		std::string x, y;
		const size_t lx = gen()%15, ly = gen()%15, pre = gen()%(std::min(lx,ly)+1);
		for(size_t i=0;i<pre;i++){ char c=(char)('a'+gen()%A); x+=c; y+=c; }
		while(x.size()<lx) x += (char)('a'+gen()%A);
		while(y.size()<ly) y += (char)('a'+gen()%A);

		const double f = p_delete_append<ap,dp>(x, y, lA);
		const double r = reference<ap,dp>(x, y, lA);
		expect(std::fabs(f-r) < 1e-9, "p('"+x+"'->'"+y+"') = "+str(f)+" but reference = "+str(r));

		// genericity: the same content as std::vector<int> must give the identical answer
		std::vector<int> xv(x.begin(), x.end()), yv(y.begin(), y.end());
		const double fv = p_delete_append<ap,dp>(xv, yv, lA);
		expect(fv == f, "vector<int> result "+str(fv)+" != string result "+str(f));
	}
}

int main(int argc, char** argv) {
	Fleet fleet("Test p_delete_append against simulation and an exact reference");
	fleet.initialize(argc, argv);

	std::mt19937_64 gen(7);

	// (1) Monte Carlo on the generative process itself
	{
		const size_t A = 2; const float lA = log((float)A);
		const std::string x = "ab";
		const long N = 10'000'000;
		std::map<std::string,long> counts;
		std::uniform_real_distribution<double> U(0,1);
		for(long i=0;i<N;i++){
			std::string s = x;
			while(s.size()>0 and U(gen)<DP_mc) s.pop_back();  // geometric deletion, forced stop at empty
			while(U(gen)<AP_mc) s += (char)('a'+gen()%A);      // geometric append, uniform characters
			if(s.size()<=4) counts[s]++;
		}
		for(auto& [y,c] : counts){
			if(c < 2000) continue; // too rare for a stable z-score
			const double phat = (double)c/N, se = sqrt(phat*(1-phat)/N);
			const double p = exp(p_delete_append<AP_mc,DP_mc>(x, y, lA));
			expect(std::fabs(phat-p) < 5*se, "MC p('ab'->'"+y+"'): simulated "+str(phat)+" vs computed "+str(p));
		}
	}

	// (2)+(3) closed form vs exact reference, and string vs vector<int>, across parameter
	// regimes. Each regime needs its own instantiation: the function latches its logs on
	// first call (its template arguments are required to be runtime constants).
	check_regime<AP1,DP1>(gen, 40000);
	check_regime<AP2,DP2>(gen, 40000);
	check_regime<AP3,DP3>(gen, 40000);
	check_regime<AP4,DP4>(gen, 40000);

	print(nfail == 0 ? "PASS" : "FAIL", str(nchecks)+" checks,", str(nfail), "failures");
	return nfail == 0 ? 0 : 1;
}
