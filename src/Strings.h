#pragma once

#include <sstream>
#include <atomic>
#include <string>
#include <string_view>


#include "Miscellaneous.h"
#include "Numerics.h"
#include "Random.h"
#include "Vector3D.h"

// This holds our conversions to and from strings
#include "cppstr.h"

// This constant is occasionally useful, especially in grammar inference where we might
// want a reference to an empty input
const std::string EMPTY_STRING = "";

const std::string LAMBDA_STRING = "\u03BB";
const std::string LAMBDAXDOT_STRING = LAMBDA_STRING+"x.";

template <typename T>
std::string to_string_with_precision(const T a_value, const int n = 14) {
	// https://stackoverflow.com/questions/16605967/set-precision-of-stdto-string-when-converting-floating-point-values
    std::ostringstream out;
    out.precision(n);
    out << std::fixed << a_value;
    return out.str();
}

/**
 * @brief Check if prefix is a prefix of x -- works with iterables, including strings and vectors
 * @param prefix
 * @param x
 * @return 
 */
/* If x is a prefix of y -- works for strings and vectors */
template<typename T>
bool is_prefix(const T& prefix, const T& x) {
	/**
	 * @brief For any number of iterable types, is prefix a prefix of x
	 * @param prefix
	 * @param x
	 * @return 
	 */
		
	if(prefix.size() > x.size()) return false;
	if(prefix.size() == 0) return true;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}

bool contains(const std::string& s, const std::string& x) {
	return s.find(x) != std::string::npos;
}

bool contains(const std::string& s, const char x) {
	return s.find(x) != std::string::npos;
}

/**
 * @brief Replace all occurances of x with y in s
 * @param s
 * @param x
 * @param y
 * @param add -- replace x plus this many characters
 */
void replace_all(std::string& s, const std::string& x, const std::string& y, int add=0) {
	// NOTE: this used to restart the search from the beginning of s after every replacement,
	// which (a) was quadratic and (b) looped forever whenever y contains x (e.g. "a" -> "aa").
	// We now continue searching after the inserted replacement, so each position is processed once.
	if(x.empty()) return; // otherwise find() matches at every position and we'd loop forever
	auto pos = s.find(x);
	while(pos != std::string::npos) {
		s.replace(pos, x.length()+add, y);
		pos = s.find(x, pos + y.length());
	}
}


/**
 * @brief Remove all characters in rem from s. TODO: Not very optimized here yeah
 * @param s
 * @param rem
 */
std::string remove_characters(std::string s, const std::string& rem) {
	// to optimize you know we could load rem into a map
	for(auto it=s.begin(); it != s.end();) {
		if(contains(rem, *it)) {
			it = s.erase(it);
		}
		else {
			++it;
		}
	}
	return s; 
}


/**
 * @brief Probability of converting x into y by deleting some number (each with del_p, then stopping with prob 1-del_p), adding with 
 * 		  probability add_p, and then when we add selecting from an alphabet of size alpha_n
 * @param x
 * @param y
 * @param del_p - probability of deleting the next character (geometric)
 * @param add_p - probability of adding (geometric)
 * @param log_alphabet - log of the size of alphabet
 * @return The probability of converting x to y by deleting characters with probability del_p and then adding with probability add_p
 */
 template<const float& add_p, const float& del_p, typename T=std::string>
inline double p_delete_append(const T& x, const T& y, const float log_alphabet) {
	/**
	 * @brief This function computes the probability that x would be converted into y, when we
	 *        delete from the end of x (each further deletion with probability del_p, forced to
	 *        stop at the empty string) and then append (each further append with probability
	 *        add_p, characters uniform on an alphabet of size exp(log_alphabet)).
	 *
	 *        Only a shared prefix of x and y can survive this process (we only ever touch the
	 *        end), so with M = the length of the longest common prefix of x and y,
	 *
	 *           p(y|x) = sum_{k=0..M} del_p^(|x|-k) * (1-del_p)^[k>0] * (add_p/A)^(|y|-k) * (1-add_p)
	 *
	 *        where k is how much of x we keep. The k=0 term has no (1-del_p) factor because
	 *        after deleting all of x the process cannot delete further, so stopping there is
	 *        forced, not a choice (this is exactly what normalizes the deletion distribution:
	 *        del_p^|x| + sum_{j<|x|} del_p^j (1-del_p) = 1).
	 *
	 *        In log space the k=1..M terms are base + log(1-del_p) + k*D where
	 *
	 *           base = |x|*log(del_p) + |y|*(log(add_p)-log A) + log(1-add_p)
	 *           D    = log A - log(del_p) - log(add_p)
	 *
	 *        and D > 0 strictly (all three summands are nonnegative and the last two are
	 *        positive for any del_p, add_p in (0,1)). So the loop this used to run is a
	 *        geometric series with an O(1) closed form,
	 *
	 *           log sum_{k=1..M} e^{kD} = M*D + log1p(-e^{-M*D}) - log1p(-e^{-D}),
	 *
	 *        which replaces M logplusexp calls (an exp and a log1p each) with constant work:
	 *        about 5x faster on prefix-heavy strings, and exact to ~1e-13 where the old
	 *        float-accumulation loop drifted by up to ~2e-5 nats on strings of length 15
	 *        (worse for longer strings, since float absolute error grows with |base|).
	 *
	 *        This is a template function because otherwise we end up computing log(add_p) and
	 *        log(del_p) on every call, and these are in fact constant. NOTE: they are latched
	 *        in function-local statics on the FIRST call of each instantiation, so the bound
	 *        template variables must never change at runtime (all current uses bind
	 *        compile-time constants like strgamma). NOTE also that add_p and del_p must lie
	 *        strictly inside (0,1): at 0 or 1 some log here is -infinity and an empty string
	 *        then produces 0 * -infinity = NaN.
	 * @param x
	 * @param y
	 * @param log_alphabet
	 * @return
	 */

	// all of these get precomputed on the first call (in double: they are computed once, so
	// there is no speed reason to round them to float, and base below is proportional to the
	// string lengths, which amplifies any rounding of the per-character terms)
	static const double log_add_p   = log((double)add_p);
	static const double log_del_p   = log((double)del_p);
	static const double log_1madd_p = log(1.0-(double)add_p);
	static const double log_1mdel_p = log(1.0-(double)del_p);

	// Well we can always delete the whole thing and add on the remainder
	// (no log_1mdel_p here since we can't delete past the beginning -- see above)
	const double base = log_del_p*(double)x.size() +
	                    (log_add_p-(double)log_alphabet)*(double)y.size() + log_1madd_p;

	// M = length of the longest common prefix of x and y (the only k>0 that can contribute).
	// No bounds-checked .at() needed: mn guards both reads.
	const size_t mn = std::min(x.size(), y.size());
	size_t M = 0;
	while(M < mn and x[M] == y[M]) ++M;

	if(M == 0) return base; // no shared prefix: deleting everything is the only way

	// the geometric series over k=1..M (see derivation above); D > 0 so both log1p
	// arguments are in (-1,0) and this is numerically stable for any M
	const double D = (double)log_alphabet - log_del_p - log_add_p;
	const double logseries = (double)M*D + log1p(-exp(-(double)M*D)) - log1p(-exp(-D));

	// logplusexp_full: the approximate logplusexp saves time by dropping small terms, but
	// here it is called just once, so we can have the exact answer at no real cost
	return logplusexp_full(base, base + log_1mdel_p + logseries);
}

std::pair<std::string, std::string> divide(const std::string& s, const char delimiter) {
	// divide this string into two pieces at the first occurance of delimiter
	auto k = s.find(delimiter);
	assert(k != std::string::npos && "*** Cannot divide a string without delimiter");
	return std::make_pair(s.substr(0,k), s.substr(k+1));
}



size_t count(const std::string& str, const std::string& sub) {
	/**
	 * @brief How many times does sub occur in str? Does not count overlapping substrings
	 * @param str
	 * @param sub
	 * @return 
	 */
	
	// https://www.rosettacode.org/wiki/Count_occurrences_of_a_substring#C.2B.2B
	
    if (sub.length() == 0) return 0;
    
	size_t count = 0;
    for (size_t offset = str.find(sub); 
		 offset != std::string::npos;
		 offset = str.find(sub, offset + sub.length())) {
        ++count;
    }
    return count;
}


size_t count(const std::string& str, const char x) {
	size_t count = 0;
	for(auto& a : str) { 
		if(a == x) 
			count++;
    }
    return count;
}


std::string reverse(std::string x) {
	return std::string(x.rbegin(),x.rend()); 
}


std::string QQ(const std::string& x) {
	/**
	 * @brief Handy adding double quotes around a string
	 * @param x - input string
	 * @return 
	 */
	
	return std::string("\"") + x + std::string("\"");
}
std::string Q(const std::string& x) {
	/**
	 * @brief Handy adding single quotes around a string 
	 * @param x - input string
	 * @return 
	 */
		
	return std::string("\'") + x + std::string("\'");
}


/**
 * @brief Check that s only uses characters from a. On failure, we print the string and assert false.
 * @param s
 * @param a
 */
void check_alphabet(const std::string& s, const std::string& a) {
	for(char c: s) {
		if(a.find(c) == std::string::npos) {
			std::cerr << "*** Character '" << c << "' in " << s << " not in alphabet=" << a << std::endl;
			assert(false);
		}
	}
}

void check_alphabet(std::vector<std::string> t, const std::string& a){
	for(auto& x : t) {
		check_alphabet(x,a);
	}
}

/**
 * @brief Compute levenshtein distiance between two strings (NOTE: Or O(N^2))
 * @param s1
 * @param s2
 * @return 
 */
unsigned int levenshtein_distance(const std::string& s1, const std::string& s2) {
	
	// From https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C++

	const std::size_t len1 = s1.size(), len2 = s2.size();
	std::vector<std::vector<unsigned int>> d(len1 + 1, std::vector<unsigned int>(len2 + 1));

	d[0][0] = 0;
	for(unsigned int i = 1; i <= len1; ++i) d[i][0] = i;
	for(unsigned int i = 1; i <= len2; ++i) d[0][i] = i;

	for(unsigned int i = 1; i <= len1; ++i)
		for(unsigned int j = 1; j <= len2; ++j)
			  d[i][j] = std::min(d[i - 1][j] + 1, std::min(d[i][j - 1] + 1, d[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1) ));
			  
	return d[len1][len2];
}


/**
 * @brief The string probability model from Kashyap & Oommen, 1983, basically giving a string
 *        edit distance that is a probability model. This is unit-tested against a brute-force
 *        enumeration of all edit paths in Testing/KashyapOommen.
 *
 * 		  This assumes that the deletions, insertions, and changes all happen with a constant, equal
 *        probability of perr, but note that swaps and insertions also have to choose the character
 *        out of nalphabet. This could probably be optimized to not have to compute these logs
 *
 *        NOTE: This previously had three bugs, all fixed now (and the fix is what the
 *        brute-force test validates):
 *          (1) every Sab call double-corrected the index: callers passed 0-indexed positions
 *              and Sab subtracted 1 again, so every substitution compared the PREVIOUS
 *              characters. Among other things this made p("ab"->"ac") == p("ab"->"ab"):
 *              a mismatch in the last position was invisible.
 *          (2) at s=1 that same shift read y[-1]/x[-1] -- out of bounds, on every call.
 *          (3) two boundary loops ran s past min(m,n-t) (resp. min(n,m-d)), filling
 *              impossible states from never-written table cells (which are 0.0 = log 1,
 *              not log 0) and reading the strings past their ends.
 * @param x
 * @param y
 * @param perr
 * @param nalphabet
 * @return
 */
double p_KashyapOommen1984_edit(const std::string& x, const std::string& y, const double perr, const size_t nalphabet) {
	// From Kashyap & Oommen, 1984
	// perr gets used here to define the probability of each of these operations
	// BUT Note here that we use the logs of these

	const int m = x.length(); // these must be ints or some negatives below go bad
	const int n = y.length();
	const int T = std::max(m,n); // max insertions allowed
	const int E = std::max(m,n); // max deletions allowed
	const int R = std::min(m,n); // max substitutions (s ranges over 0..R, so the table's third dim is R+1)

	const double lp_insert = -log(nalphabet); // P(y_t|lambda) - probability of generating a single character from nothing
	const double lp_delete = log(perr);           // P(phi|d_x) -- probability of deleting

	// filled with -infinity: a state we never reach has probability 0, and log(0) = -inf.
	// (A default-constructed table is 0.0 everywhere, which is log(1)!)
	Vector3D<double> mytable(T+1,E+1,R+1,-infinity);

	// set to 1 in log space
	mytable(0,0,0) = 0.0;

	// Substitution probability for producing y[i] from x[j], with i,j 0-INDEXED positions
	// (the loops below pass t+s-1 and d+s-1, which are already 0-indexed: after t insertions
	// and s substitutions we have produced characters y[0..t+s-1], so the s-th substitution
	// produced y[t+s-1], consuming x[d+s-1]).
	// Note that when we are correct, we sum up both ways of getting the answer
	const auto Sab = [&](int i, int j) -> double {
		return log(perr / nalphabet + (1.0-perr)*(y[i]==x[j] ? 1 : 0));
	};

	for(int t=1;t<=T;t++)
		mytable(t,0,0) = mytable(t-1,0,0) + lp_insert;

	for(int d=1;d<=E;d++)
		mytable(0,d,0) = mytable(0,d-1,0) + lp_delete;

	for(int s=1;s<=R;s++)
		mytable(0,0,s) = mytable(0,0,s-1) + Sab(s-1,s-1);

	for(int t=1;t<=T;t++)
		for(int d=1;d<=E;d++)
			mytable(t,d,0) = logplusexp(mytable(t-1,d,0)+lp_insert,
								        mytable(t,d-1,0)+lp_delete);

	// s substitutions also consume s characters of x, so s <= m here (and s <= n below):
	// without that bound we'd fill impossible states, reading the strings out of bounds to do it
	for(int t=1;t<=T;t++)
		for(int s=1;s<=std::min(n-t,m);s++)
			mytable(t,0,s) = logplusexp(mytable(t-1,0,s)+lp_insert,
									    mytable(t,0,s-1)+Sab(s+t-1,s-1));

	for(int d=1;d<=E;d++)
		for(int s=1;s<=std::min(m-d,n);s++)
			mytable(0,d,s) = logplusexp(mytable(0,d-1,s)+lp_delete,
								        mytable(0,d,s-1)+Sab(s-1,s+d-1));

	for(int t=1;t<=T;t++)
		for(int d=1;d<=E;d++)
			for(int s=1;s<=std::min(n-t,m-d);s++)
				mytable(t,d,s) = logplusexp(mytable(t-1,d,s)+lp_insert,
								 logplusexp(mytable(t,d-1,s)+lp_delete,
								            mytable(t,d,s-1)+Sab(t+s-1,d+s-1))); // shoot me in the face

	// now we sum up
	double lp_yGx = -infinity; // p(Y|X)
	for(int t=std::max(0,n-m); t<=std::min(T,E+n-m);t++){
		double qt = geometric_lpdf(t+1,1.0-perr); // probability of t insertions -- but must use t+1 to allow t=0 (geometric is defined on 1,2,3,...)
		lp_yGx = logplusexp(lp_yGx, mytable(t,m-n+t,n-t) + qt + lfactorial(m)+lfactorial(t)-lfactorial(m+t));
	}
	return lp_yGx;

}


////////////////////////////////////////////////////////////
// Printing of types  (ugly, sorry)

template <typename T>
constexpr std::string_view type_name() {
#if defined(__clang__)
    std::string_view s = __PRETTY_FUNCTION__;
    return { s.data() + 31, s.size() - 31 - 1 };
#elif defined(__GNUC__)
    std::string_view s = __PRETTY_FUNCTION__;
    return { s.data() + 46, s.size() - 46 - 1 };
#elif defined(_MSC_VER)
    std::string_view s = __FUNCSIG__;
    return { s.data() + 38, s.size() - 38 - 7 };
#else
    return "unsupported compiler for type_name";
#endif
}

template<typename... Ts>
constexpr auto types2string() {
    return std::array{ type_name<Ts>()... };
}
