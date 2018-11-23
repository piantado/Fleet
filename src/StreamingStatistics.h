#ifndef STREAMING_STATISTICS
#define STREAMING_STATISTICS


template<typename T>
class ReservoirSample {
public:

	class Item {
	public:
		T x;
		double r; 
		
		Item(T x_, double r_) : x(x_), r(r_) {}
		Item(const Item &i)        : x(i.x), r(i.r) { }
		
		bool operator<(const Item& b) const {
			return r < b.r;
		}
		
		bool operator==(const Item& b) const {
			return x==b.x && r==b.r;
		}
	};
	
public:

	std::multiset<Item>   s; //
	std::multiset<T> vals; // store this so we can efficiently find the median
	size_t reservoir_size;
	bool unique; // do I allow multiple samples?
	unsigned long N; // how many have I seen? An time I *try* to add something to this, N gets incremented
	
protected:
	pthread_mutex_t lock;		

public:

	ReservoirSample(size_t n, bool u=false) : reservoir_size(n), unique(u), N(0) {
		pthread_mutex_init(&lock, nullptr);
	}
	
	ReservoirSample(bool u=false) : reservoir_size(1000), unique(u), N(0) {
		pthread_mutex_init(&lock, nullptr);
	}
	
	void set_size(const size_t s) const {
		reservoir_size = s;
	}
	
	size_t size() const {
		// How many am I currently storing?
		return s.size();
	}
	void set_size(size_t n) {
		reservoir_size = n;
	}
	
	T max(){ return *vals.rbegin(); }
	T min(){ return *vals.begin(); }
	
	void add(T x) {
		pthread_mutex_lock(&lock);
		
		if((!unique) || vals.find(x) == vals.end()) {
		
			double r = uniform(rng); // TODO: Can check r first here before doing all that below
			s.insert(Item(x,r));
			
			vals.insert(x);
			while(s.size() > reservoir_size) {
				auto i = s.begin(); // which one we remove -- last one since set stores things sorted
				auto pos = vals.find(x); 
				assert(pos != vals.end());
				vals.erase(pos); // this only deletes one? 
				s.erase(i); // erase the item
				// NOTE: we might sometimes get identical x and r, and then this causes problems because it will delete all? Very rare though...
			}
			
			assert(s.size() == vals.size());
		}
		
		++N;
		
		pthread_mutex_unlock(&lock);
	}
	void operator<<(T x) {	add(x);}
	
	// Return a sample from my vals (e.g. a sample of the samples I happen to have saved)
	T sample() const {
		assert(N > 0);
		std::uniform_int_distribution<int> sampler(0,vals.size()-1);
		auto pos = vals.begin();
		std::advance(pos, sampler(rng));
		return *pos;
	}
	
	auto begin() {
		return vals.begin();
	}
	auto end() {
		return vals.end();
	}
};



template<typename T>
class MedianFAME {
	// A streaming median class implementing the FAME algorithm
	// Here, we initialize both the step size and M with the current sample
	// http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.108.7376&rep=rep1&type=pdf
public:

	T M;
	size_t n;
	T step;
	
	MedianFAME() : M(0.0), n(0), step(NaN) {
	}	
		
	T median() const { 
		return M;
	}
	
	void add(T x) {
		
		if(n==0) {
			M = x;
			step = sqrt(abs(x)); // seems bad to initialize at x b/c then we start with giant sign swings; sqrt has a nice aesthetic
		}
		else {
			
			if(x > M) {
				M += step;
			}
			else if(x <= M) {
				M -= step;
			}
			
			if(abs(x-M) < step) {
				step = step/2.0;
			}
		}
		++n;
	}
	void operator<<(T x) {	add(x);}

	
};



class StreamingStatistics {
	// a class to store a bunch of statistics about incoming data points, including min, max, mean, etc. 
	// also stores a reservoir sample and allow us to compute how often one distribution exceeds another
	
protected:
	pthread_mutex_t lock;		
	
public:

	double min;
	double max;
	double sum;
	double lse; // logsumexp
	double N;
	
	MedianFAME<double>      streaming_median;
	ReservoirSample<double> reservoir_sample;
	
	StreamingStatistics(size_t rs=1000) : 
		min(infinity), max(-infinity), sum(0.0), lse(-infinity), N(0), reservoir_sample(rs) {
		pthread_mutex_init(&lock, nullptr);
	}

	void add(double x) {
		++N; // always count N, even if we get nan/inf (this is required for MCTS, otherwise we fall into sampling nans)
		if(std::isnan(x) || std::isinf(x)) return; // filter nans and inf(TODO: Should we filter inf?)
		
		pthread_mutex_lock(&lock);

		streaming_median << x;
		reservoir_sample << x;
		
		if(x < min) min = x;
		if(x > max) max = x;
		
		if(! std::isinf(x) ) { // we'll filter zeros from this
			sum += x;
			lse = logplusexp(lse, x);
		}
		
		pthread_mutex_unlock(&lock);
	}
	
	void operator<<(double x) { 
		add(x);
	}
	
	double sample() const {
		return reservoir_sample.sample();
	}
	
	double median() const {
		
		if(reservoir_sample.size() == 0) return -infinity;
		else {
			auto pos = reservoir_sample.vals.begin();
			std::advance(pos,reservoir_sample.size()/2);
			return *pos;
		}
		//return streaming_median.median();
	}
	
	void print() const {
		for(auto a : reservoir_sample.s){
			std::cout << a.x << "[" << a.r << "]" << std::endl;
		}
	}
	
	double p_exceeds_median(const StreamingStatistics &q) const {
		// how often do I exceed the median of q?
		size_t k = 0;
		double qm = median();
		for(auto a : reservoir_sample.s) {
			if(a.x > qm) k++;
		}
		return double(k)/reservoir_sample.size();
	}
	
//	double p_exceeds(const StreamingStatistics &q) const {
//		//https://stats.stackexchange.com/questions/71531/probability-that-a-draw-from-one-sample-is-greater-than-a-draw-from-a-another-sa
//		
//		size_t sum = 0;
//		
//		size_t i=0; // where am I in q?
//		for(auto a: q.sample.vals) {
//			auto pos = sample.vals.lower_bound(a); // where would a have gone?
//			size_t d = std::distance(sample.vals.begin(), pos); // NOTE: NOT EFFICIENT
//			size_t rank = d + i; // my overall rank
//			
//			sum += 
//			i++;
//		}
//	}
	
};

#endif