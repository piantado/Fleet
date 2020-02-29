#pragma once


#include <string.h>

template<typename T>
std::string str(T x){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
		
	return std::to_string(x);
}

/**
 * @brief Probability of converting x into y by deleting some number (each with del_p, then stopping with prob 1-del_p), adding with 
 * 		  probability add_p, and then when we add selecting from an alphabet of size alpha_n
 * @param x
 * @param y
 * @param del_p - probability of deleting the next character (geometric)
 * @param add_p - probability of adding (geometric)
 * @param alpha_n - size of alphabet
 * @return The probability of converting x to y by deleting characters with probability del_p and then adding with probability add_p
 */
inline double p_delete_append(const std::string& x, const std::string& y, double del_p, double add_p, double alpha_n) {
	
	// Well we can always delete the whole thing and add on the remainder
	double lp = log(del_p)*x.length()         + log(1.0-del_p) + 
				log(add_p/alpha_n)*y.length() + log(1.0-add_p);
	
	// now as log as they are equal, we can take only down that far if we want
	// here we index over mi, the length of the string that so far is equal
	for(size_t mi=1;mi<=std::min(x.length(),y.length());mi++){
		if(x[mi-1] == y[mi-1]) {
			lp = logplusexp(lp, log(del_p)*(x.length()-mi)         + log(1.0-del_p) + 
							    log(add_p/alpha_n)*(y.length()-mi) + log(1.0-add_p));
		}
		else {
			break;
		}
	}
	
//	CERR lp TAB x TAB y ENDL;
	
	return lp;
}


std::deque<std::string> split(const std::string& s, const char delimiter){
	/**
	 * @brief Split is returns a deque of s split up at the character delimiter. 
	 * It handles these special cases:
	 * split("a:", ':') -> ["a", ""]
	 * split(":", ':')  -> [""]
	 * split(":a", ':') -> ["", "a"]
	 * @param s
	 * @param delimiter
	 * @return 
	 */
		
	std::deque<std::string> tokens;
	
	if(s.length() == 0) {
		return tokens;
	}
	
	size_t i = 0;
	while(i < s.size()) {
		size_t k = s.find(delimiter, i);
		
		if(k == std::string::npos) {
			tokens.push_back(s.substr(i,std::string::npos));
			return tokens;
		}
		else {		
			tokens.push_back(s.substr(i,k-i));
			i=k+1;
		}
	}	

	// if we get here, that means that the last foudn k was the last
	// character, which means we need to append ""
	tokens.push_back("");
	return tokens;
}

std::pair<std::string, std::string> divide(const std::string& s, const char delimiter) {
	// divide this string into two pieces at the first occurance of delimiter
	
	auto k = s.find(delimiter);
	assert(k != std::string::npos && "*** Cannot divide a string without delmiiter");
	return std::make_pair(s.substr(0,k), s.substr(k+1));
	
}



unsigned int levenshtein_distance(const std::string& s1, const std::string& s2) {
	/**
	 * @brief Compute levenshtein distiance between two strings (NOTE: Or O(N^2))
	 * @param s1
	 * @param s2
	 * @return 
	 */
	
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


size_t count(const std::string& str, const std::string& sub) {
	/**
	 * @brief How many times does sub occur in str?
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


std::string QQ(std::string x) {
	/**
	 * @brief Handy adding double quotes to a string
	 * @param x - input string
	 * @return 
	 */
	
	return std::string("\"") + x + std::string("\"");
}
std::string Q(std::string x) {
	/**
	 * @brief Handy adding single quotes to a string 
	 * @param x - input string
	 * @return 
	 */
		
	return std::string("\'") + x + std::string("\'");
}


// Some functions for converting to and from strings:
//namespace Fleet {
//	
//	
//	
//	const char NodeDelimiter = ';';
//	const char FactorDelimiter = '|';
//	
//	
//	
//}