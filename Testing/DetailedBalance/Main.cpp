// Flow-balance test for the regenerate / insert_tree / delete_tree proposals.
//
// (This replaces the old DetailedBalance test, which (a) no longer compiled against the 
// optional-returning propose(), and (b) compared empirical marginal transition frequencies 
// against a single reported fb -- a criterion that is only valid when fb is a function of 
// (a,b). The corrected insert/delete corrections are PATHWISE (they depend on the sampled 
// path, e.g. on the generated-and-discarded subtree's slot count), which is a valid form of 
// MH, so the right test is flow balance of the actual dynamics, below.)
//
// We run Metropolis-Hastings with the proposals under test, targeting the grammar PRIOR
// pi(x) = exp(grammar.log_probability(x)) -- a distribution we know exactly, with no
// likelihood/VM involved. If the proposals' forward-backward corrections are right, then
//   (1) for every pair of states, the number of accepted a->b transitions equals the number
//       of accepted b->a transitions (up to binomial noise), and
//   (2) the chain's empirical state frequencies match pi, i.e.
//       log(freq(x)/freq(y)) == lp(x) - lp(y) for common states x,y.
// Both hold for ANY valid MH kernel, including ones whose fb depends on the sampled path.

#define NO_INCLUDE_MAIN 1
#include "../../Models/RationalRules/Main.cpp"   // a small, discrete grammar so outcomes concentrate

#include "Fleet.h"
#include <map>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv){
	Fleet fleet("proposal flow-balance test");
	fleet.initialize(argc, argv);

	const size_t STEPS = 20000000;

	// start somewhere smallish
	Node x = grammar.generate();
	while(x.count() > 6) x = grammar.generate();
	double lp_x = grammar.log_probability(x);
	std::string xs = x.string();

	std::map<std::pair<std::string,std::string>, long> flow; // accepted transitions
	std::map<std::string, size_t> occ;                        // state occupancy
	std::map<std::string, double> lp_of;                      // state -> prior lp

	size_t accepts = 0;
	for(size_t i=0;i<STEPS;i++) {
		std::optional<std::pair<Node,double>> p;
		// the same mixture FormalLanguageTheory-Simple uses: regenerate half the time, 
		// else insert or delete with equal probability (equal mixture weights cancel in fb)
		if(flip(0.5)) p = Proposals::regenerate(&grammar, x);
		else if(flip()) p = Proposals::insert_tree(&grammar, x);
		else            p = Proposals::delete_tree(&grammar, x);

		if(p) {
			const double lp_b = grammar.log_probability(p->first);
			if(log(uniform()) < lp_b - lp_x - p->second) {
				std::string bs = p->first.string();
				if(bs != xs) { flow[{xs,bs}]++; accepts++; }
				x = std::move(p->first);
				lp_x = lp_b;
				xs = std::move(bs);
			}
		}
		occ[xs]++;
		if(not lp_of.count(xs)) lp_of[xs] = lp_x;
	}
	printf("steps=%zu  accepted (non-identity)=%zu  distinct states=%zu\n\n", STEPS, accepts, occ.size());

	// -------- (1) pairwise flow balance --------
	printf("flow balance (pairs with >= 1000 transitions in either direction):\n");
	printf("%-32s %-32s %8s %8s %7s\n", "a", "b", "a->b", "b->a", "sigma");
	int nbad_flow=0, npairs=0;
	for(auto& [k, cf] : flow) {
		auto& [as, bs] = k;
		if(as >= bs) continue; // each unordered pair once
		long cb = flow.count({bs,as}) ? flow[{bs,as}] : 0;
		if(cf < 1000 and cb < 1000) continue;
		const double sig = (cf - cb) / sqrt((double)(cf + cb));
		npairs++;
		if(std::abs(sig) > 5) { nbad_flow++;
			printf("%-32s %-32s %8ld %8ld %7.1f  ***\n", as.substr(0,32).c_str(), bs.substr(0,32).c_str(), cf, cb, sig);
		}
	}
	printf("  ... %d/%d frequent pairs violate flow balance at 5 sigma\n\n", nbad_flow, npairs);

	// -------- (2) occupancy matches the prior --------
	printf("occupancy vs prior (states visited >= 20000 times), relative to the most-visited state:\n");
	std::string ref; size_t refc=0;
	for(auto& [s,c] : occ) if(c > refc) { refc=c; ref=s; }
	printf("%-44s %10s %10s %7s\n", "state", "emp_logR", "prior_logR", "sigma");
	int nbad_occ=0, nstates=0;
	for(auto& [s,c] : occ) {
		if(c < 20000 or s == ref) continue;
		const double emp = log((double)c/refc);
		const double tru = lp_of[s] - lp_of[ref];
		// crude SE: occupancy samples are autocorrelated; scale binomial SE by sqrt(tau)~10
		const double se = sqrt(1.0/c + 1.0/refc) * 10.0;
		const double sig = (emp - tru)/se;
		nstates++;
		if(std::abs(sig) > 5) { nbad_occ++;
			printf("%-44s %10.4f %10.4f %7.1f  ***\n", s.substr(0,44).c_str(), emp, tru, sig);
		}
	}
	printf("  ... %d/%d frequent states deviate from the prior at 5 sigma (SE inflated x10 for autocorrelation)\n", nbad_occ, nstates);

	return (nbad_flow==0 and nbad_occ==0) ? 0 : 1;
}
