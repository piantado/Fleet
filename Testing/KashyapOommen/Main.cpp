/* Unit test for p_KashyapOommen1984_edit (src/Strings.h).
 *
 * The DP in p_KashyapOommen1984_edit computes
 *
 *    p(y|x) = sum_t  qt * [ m! t! / (m+t)! ] * sum_{paths with t insertions} prod(op factors)
 *
 * where a path is an interleaved sequence of
 *    insertions    (produce one y char from nothing; factor 1/nalphabet)
 *    deletions     (consume one x char;              factor perr)
 *    substitutions (consume x[j], produce y[i];      factor perr/nalphabet + (1-perr)*[x[j]==y[i]])
 * capped at T = max(m,n) insertions, and qt = geometric_lpdf(t+1, 1-perr) = perr^t (1-perr).
 *
 * This test recomputes the same quantity by BRUTE FORCE: it enumerates every edit path
 * explicitly, with unambiguous character indexing (the substitution that consumes x[j] and
 * produces y[i] compares exactly those two characters). Agreement is required to within
 * logplusexp's documented approximation tolerance (it drops terms ~1e-6 below the max).
 *
 * This test catches the three bugs fixed in p_KashyapOommen1984_edit (see its docstring):
 * the double index-correction in Sab (which, e.g., made p("ab"->"ac") == p("ab"->"ab")),
 * the out-of-bounds string reads at s=1, and the boundary loops that filled impossible
 * states from never-written (0.0 = log 1) table cells.
 */

#include <string>
#include <map>
#include <functional>
#include <cmath>

#include "Fleet.h"
#include "Strings.h"

// brute-force ground truth (linear space; fine for the tiny strings used here)
double brute(const std::string& x, const std::string& y, double perr, size_t nalpha) {
	const int m = x.size(), n = y.size();
	const int T = std::max(m,n);
	std::map<int,double> wt; // t -> total weight of all paths with t insertions
	std::function<void(int,int,int,double)> rec = [&](int j, int i, int t, double w) {
		if(j==m and i==n) { wt[t] += w; return; }
		if(i<n and t<T) rec(j, i+1, t+1, w*(1.0/nalpha));                                  // insertion
		if(j<m)         rec(j+1, i,   t, w*perr);                                          // deletion
		if(j<m and i<n) rec(j+1, i+1, t, w*(perr/nalpha + (1.0-perr)*(x[j]==y[i]?1:0)));   // substitution
	};
	rec(0,0,0,1.0);
	double total = 0.0;
	for(auto& [t,w] : wt) {
		double qt = pow(perr,t)*(1.0-perr);  // geometric_lpdf(t+1, 1-perr), linear space
		double invC = 1.0;                   // m! t! / (m+t)! = 1/C(m+t,t)
		for(int k=1;k<=t;k++) invC *= (double)k/(m+k);
		total += w * qt * invC;
	}
	return log(total);
}

int main(int argc, char** argv) {
	Fleet fleet("Test p_KashyapOommen1984_edit against brute-force path enumeration");
	fleet.initialize(argc, argv);

	const double TOL = 1e-5; // logplusexp drops terms ~1e-6 below the max; allow a few of those
	size_t nchecks = 0, nfail = 0;

	auto check = [&](const std::string& x, const std::string& y, double perr, size_t na) {
		const double b = brute(x, y, perr, na);
		const double f = p_KashyapOommen1984_edit(x, y, perr, na);
		nchecks++;
		if(std::fabs(f-b) > TOL) {
			nfail++;
			print("FAIL: p('"+x+"'->'"+y+"')", "perr="+str(perr), "nalphabet="+str(na),
			      "brute="+str(b), "dp="+str(f));
		}
	};

	// directed cases (include the shapes that exercised each old bug)
	for(double perr : {0.01, 0.1, 0.4}) {
		for(size_t na : {2ul, 4ul, 26ul}) {
			check("ab","ab",perr,na);  check("ab","ac",perr,na);   // old code: these were EQUAL
			check("ab","cb",perr,na);  check("ab","ba",perr,na);
			check("a","a",perr,na);    check("a","b",perr,na);
			check("","a",perr,na);     check("a","",perr,na);      check("","",perr,na);
			check("abc","acb",perr,na);
			check("a","aaa",perr,na);  check("aaa","a",perr,na);   // old code: read unwritten cells here
			check("ab","abab",perr,na);check("abab","ab",perr,na);
			check("dcba","abcd",perr,na);
		}
	}

	// randomized sweep
	std::mt19937 gen(12345);
	for(int trial=0; trial<4000; trial++) {
		double perr = 0.01 + 0.4*std::uniform_real_distribution<double>(0,1)(gen);
		size_t na = 2 + gen()%8;
		std::string x, y;
		for(size_t i=0, lx=gen()%6; i<lx; i++) x += (char)('a' + gen()%na);
		for(size_t i=0, ly=gen()%6; i<ly; i++) y += (char)('a' + gen()%na);
		check(x, y, perr, na);
	}

	print(nfail == 0 ? "PASS" : "FAIL", str(nchecks)+" checks,", str(nfail), "failures");
	return nfail == 0 ? 0 : 1;
}
