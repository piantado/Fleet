#ifndef MISCELLANEOUS_H
#define MISCELLANEOUS_H

#include<random>
#include<cmath>

// so sick of typing this horseshit
#define TAB <<"\t"<< 
#define ENDL <<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<

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


std::string Q(std::string x) {
	return std::string("\"") + x + std::string("\"");
}

/* If x is a prefix of y */
bool is_prefix(const std::string prefix, const std::string x) {
	//https://stackoverflow.com/questions/7913835/check-if-one-string-is-a-prefix-of-another
	
	if(prefix.length() > x.length()) return false;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}

#endif