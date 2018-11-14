#ifndef MISCELLANEOUS_H
#define MISCELLANEOUS_H

#include<random>
#include<cmath>
#include <deque>
#include <iostream>
#include <sstream>


// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
	 
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _b : _a; })


const double NaN = std::numeric_limits<double>::quiet_NaN();;
const double pi  = 3.141592653589793238;

std::random_device rd;     // only used once to initialise (seed) engine
std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
std::uniform_real_distribution<double> uniform(0,1.0);

const double LOG05 = -log(2.0);

inline size_t hash_combine(size_t a, size_t b) {
    return (a^(a + 0x9e3779b9 + (b<<6) + (b>>2)));
}

const double infinity = std::numeric_limits<double>::infinity();

double logplusexp(const double a, const double b) {
    if(a==-infinity) return b;
    if(b==-infinity) return a;
    
    double m = (a>b?a:b);
    return m + log(exp(a-m)+exp(b-m));
}

double cauchy_lpdf(double x, double loc=0.0, double gamma=1.0) {
    return -log(pi) - log(gamma) - log(1+((x-loc)/gamma)*((x-loc)/gamma));
}

double normal_lpdf(double x, double mu=0.0, double sd=1.0) {
    //https://stackoverflow.com/questions/10847007/using-the-gaussian-probability-density-function-in-c
    const float linv_sqrt_2pi = -0.5*log(2*pi*sd);
    return linv_sqrt_2pi  - ((x-mu)/sd)*((x-mu)/sd) / 2.0;
}

double random_cauchy() {
	return tan(pi*(uniform(rng)-0.5));
}


std::string QQ(std::string x) {
	return std::string("\"") + x + std::string("\"");
}
std::string Q(std::string x) {
	return std::string("\'") + x + std::string("\'");
}

/* If x is a prefix of y */
bool is_prefix(const std::string prefix, const std::string x) {
	//https://stackoverflow.com/questions/7913835/check-if-one-string-is-a-prefix-of-another
	
	if(prefix.length() > x.length()) return false;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}


std::deque<std::string> split(const std::string& s, const char delimiter)
{
   std::deque<std::string> tokens;
   std::string token;
   std::istringstream ts(s);
   while (std::getline(ts, token, delimiter))
   {
      tokens.push_back(token);
   }
   return tokens;
}


// From https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C++
unsigned int levenshtein_distance(const std::string& s1, const std::string& s2)
{
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

template<typename T>
T myrandom(T max) {
	std::uniform_int_distribution<T> r(0,max-1);
	return r(rng);
}


#endif