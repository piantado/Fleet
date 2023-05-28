#pragma once

#include <sstream>
#include <atomic>
#include <string>

#include "Miscellaneous.h"
#include "Numerics.h"
#include "Random.h"
#include "Vector3D.h"

// This holds our conversions to and from strings
#include "str.h"

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
	auto pos = s.find(x);
	while(pos != std::string::npos) {
		s.replace(pos,x.length()+add,y);
		pos = s.find(x);
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
 template<const float& add_p, const float& del_p>
inline double p_delete_append(const std::string& x, const std::string& y, const float log_alphabet) {
	/**
	 * @brief This function computes the probability that x would be converted into y, when we insert with probability add_p and delete with probabiltiy del_p
	 * 		  and when we add we add from an alphabet of size log_alphabet. Note that this is a template function because otherwise
	 *        we end up computing log(add_p) and log(del_p) a lot, and these are in fact constant. 
	 * @param x
	 * @param y
	 * @param log_alphabet
	 * @return 
	 */
	
	// all of these get precomputed at compile time 
	static const float log_add_p   = log(add_p);
	static const float log_del_p   = log(del_p);
	static const float log_1madd_p = log(1.0-add_p);
	static const float log_1mdel_p = log(1.0-del_p);	
	
	
	// Well we can always delete the whole thing and add on the remainder
	float lp = log_del_p*x.length()                 + // we don't add log_1mdel_p again here since we can't delete past the beginning
			   (log_add_p-log_alphabet)*y.length() + log_1madd_p;
	
	// now as long as they are equal, we can take only down that far if we want
	// here we index over mi, the length of the string that so far is equal
	for(size_t mi=1;mi<=std::min(x.length(),y.length());mi++){
		if(x[mi-1] == y[mi-1]) {
			lp = logplusexp(lp, log_del_p*(x.length()-mi)                + log_1mdel_p + 
							    (log_add_p-log_alphabet)*(y.length()-mi) + log_1madd_p);
		}
		else {
			break;
		}
	}
	
	return lp;
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
	 * @brief Handy adding double quotes to a string
	 * @param x - input string
	 * @return 
	 */
	
	return std::string("\"") + x + std::string("\"");
}
std::string Q(const std::string& x) {
	/**
	 * @brief Handy adding single quotes to a string 
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
 *        edit distance that is a probability model. This could really use some unit tests,
 *        but it is hard to find implementations. 
 * 			
 * 		  This assumes that the deletions, insertions, and changes all happen with a constant, equal
 *        probability of perr, but note that swaps and insertions also have to choose the character
 *        out of nalphabet. This could probably be optimized to not have to compute these logs
 * @param x
 * @param y
 * @param perr
 * @param nalphabet
 * @return 
 */
double p_KashyapOommen1984_edit(const std::string x, const std::string y, const double perr, const size_t nalphabet) {
	// From Kashyap & Oommen, 1984
	// perr gets used here to define the probability of each of these operations
	// BUT Note here that we use the logs of these 
	
	const int m = x.length(); // these must be ints or some negatives below go bad
	const int n = y.length();
	const int T = std::max(m,n); // max insertions allowed
	const int E = std::max(m,n); // max deletions allowed
	const int R = std::min(m,n);
	
	const double lp_insert = -log(nalphabet); // P(y_t|lambda) - probability of generating a single character from nothing
	const double lp_delete = log(perr);           // P(phi|d_x) -- probability of deleting
	
	Vector3D<double> mytable(T+1,E+1,T+1);
	
	// set to 1 in log space
	mytable(0,0,0) = 0.0; 
	
	// indexes into y and x, and returns the swap probability
	// when they are different. Also corrects so i,j are 0-indexed
	// instead of 1-indexed, as they are below.
	// Note that when we are correct, we sum up both ways of getting the answer
	const auto Sab = [&](int i, int j) -> double {	
		return log(perr / nalphabet + (1.0-perr)*(y[i-1]==x[j-1] ? 1 : 0));
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
			
	for(int t=1;t<=T;t++)
		for(int s=1;s<=n-t;s++)
			mytable(t,0,s) = logplusexp(mytable(t-1,0,s)+lp_insert,
									    mytable(t,0,s-1)+Sab(s+t-1,s-1));
	
	for(int d=1;d<=E;d++)
		for(int s=1;s<=m-d;s++) 
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

