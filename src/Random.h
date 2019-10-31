#pragma once

#include <random>
#include <functional>
std::normal_distribution<float> normal(0.0, 1.0);
		
class UniformDistribution {
	/* We define a uniform distribution (rather than a global one) because 
	 * otherwise there is a block in multithreading in accessing the same rng.
	 * NOTE: Seeding does not quite work here because we don't guarantee that thread_seed is set consistently across threads
	 * */
	
	std::random_device rd;     // only used once to initialise (seed) engine
	std::mt19937 rng;    // random-number engine used (Mersenne-Twister in this case)
	std::uniform_real_distribution<double> u;
	
	static std::atomic<uintmax_t> thread_seed; // when we seed, this must be shared across threads
public:
	UniformDistribution() : u(0,1) {
		if(random_seed != 0)  {
			++thread_seed;
			rng.seed(thread_seed*random_seed);
		}
		else {
			rng.seed(rd());
		}
	}
	
	double operator() () {
		return u(rng);
	}
};
std::atomic<uintmax_t> UniformDistribution::thread_seed = 0;


double cauchy_lpdf(double x, double loc=0.0, double gamma=1.0) {
    return -log(pi) - log(gamma) - log(1+((x-loc)/gamma)*((x-loc)/gamma));
}

double normal_lpdf(double x, double mu=0.0, double sd=1.0) {
    //https://stackoverflow.com/questions/10847007/using-the-gaussian-probability-density-function-in-c
    const float linv_sqrt_2pi = -0.5*log(2*pi*sd);
    return linv_sqrt_2pi  - ((x-mu)/sd)*((x-mu)/sd) / 2.0;
}

double random_cauchy() {
	UniformDistribution uniform;
	return tan(pi*(uniform()-0.5));
}

template<typename t>
std::vector<t> random_multinomial(t alpha, size_t len) {
	// give back a random multinomial distribution 
	std::random_device rd;     // only used once to initialise (seed) engine
	std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)	
	std::gamma_distribution<t> g(alpha, 1.0);
	std::vector<t> out(len);
	t total = 0.0;
	for(size_t i =0;i<len;i++){
		out[i] = g(rng);
		total += out[i];
	}
	for(size_t i=0;i<len;i++){
		out[i] /= total;
	}
	return out;	
}

bool flip() {
	UniformDistribution uniform;
	return uniform() < 0.5;
}

template<typename t, typename T> 
std::pair<t*,double> sample(const T& s, std::function<double(const t&)>& f = [](const t& v){return 1.0;}) {
	// this takes a collection T of elements t, and a function f
	// which assigns them each a probability, and samples from them according
	// to the probabilities in f
	UniformDistribution uniform;
	
	double z = 0.0; // find the normalizer
	for(auto& x : s) 
		z += f(x);
	
	double r = z * uniform();
	
	for(auto& x : s) {
		double fx = f(x);
		r -= fx;
		if(r <= 0.0) 
			return std::make_pair(const_cast<t*>(&x), log(fx)-log(z));
	}
	
	assert(0 && "*** Should not get here in sampling");	
}