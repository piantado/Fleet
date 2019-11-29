#pragma once

template<typename T>
class ReservoirSample {
	
	// A special weighted reservoir sampling class that allows for logarithmic weights
	// to do this, we use a transformation following https://en.wikipedia.org/wiki/Reservoir_sampling#Weighted_random_sampling_using_Reservoir
	// basically, we want to give a weight that is r^1/w, 
	// or log(r)/w, or log(log(r))-log(w). But the problem is that log(r) is negative so log(log(r)) is not defined. 
	// Instead, we'll use the function f(x)=-log(-log(x)), which is monotonic. So then,
	// -log(-log(r^1/w)) = -log(-log(r)/w) = -log(-log(r)*1/w) = -[log(-log(r)) - log(w)] = -log(-log(r)) + log(w)
	
public:

	class Item {
	public:
		T x;
		const double r; 
		const double lw; // the log weight -- passed in in log form (as in a posterior probability)
		const double lv; // my value -- a function of my r and w.
		
		Item(T x_, double r_, double lw_=0.0) : x(x_), r(r_), lw(lw_), 
			lv(-log(-log(r)) + lw_) {}
		
		bool operator<(const Item& b) const {
			return lv < b.lv;
		}
		
		bool operator==(const Item& b) const {
			return x==b.x && r==b.r && lw==b.lw;
		}
	};
	
public:

	std::multiset<Item>   s; //
	std::multiset<T> vals; // store this so we can efficiently check membership in the set (otherwise its items, sorted by values)
	size_t reservoir_size;
	bool unique; // do I allow multiple samples?
	unsigned long N; // how many have I seen? An time I *try* to add something to this, N gets incremented
	
protected:
	//mutable std::mutex lock;		

public:

	ReservoirSample(size_t n, bool u=false) : reservoir_size(n), unique(u), N(0) {}	
	ReservoirSample(bool u=false) : reservoir_size(1000), unique(u), N(0) {}
	
	void set_size(const size_t s) const {
		reservoir_size = s;
	}
	
	size_t size() const {
		// How many am I currently storing?
		return s.size();
	}
	
	T max(){ return *vals.rbegin(); }
	T min(){ return *vals.begin(); }
	auto begin() { return vals.begin(); }
	auto end()   { return vals.end(); }

	double best_posterior() const {
		// what is the best posterior score here?		
		double bp = -infinity;
		for(const auto& h : vals) {
			if(h.posterior > bp) 
				bp = h.posterior;
		}
		return bp;
	}

	void add(T x, double lw=0.0) {
		//std::lock_guard guard(lock);
		
		if((!unique) || vals.find(x) == vals.end()) {
		
			double r = uniform();
			s.insert(Item(x,r,lw));
						
			vals.insert(x);
			
			while(s.size() > reservoir_size) {
				auto y   = s.begin(); // which one we remove -- first one since set stores things sorted
				auto pos = vals.find(y->x); assert(pos != vals.end());
				vals.erase(pos); // this only deletes one? 
				s.erase(y); // erase the item
				// NOTE: we might sometimes get identical x and r, and then this causes problems because it will delete all? Very rare though...
			}
			
			assert( (!unique) or s.size() == vals.size());
		}
		
		++N;
		
	}
	void operator<<(T x) {	add(x);}
	
	// Return a sample from my vals (e.g. a sample of the samples I happen to have saved)
	T sample() const {
		if(N == 0) return NaN;
		
		
		double lz = -infinity;
		for(auto& i : s) {
			lz = logplusexp(lz, i.lw); // the log normalizer
		}
		
		double zz = -infinity;
		double r = log(uniform()) + lz; // random number -- uniform*z 
		for(auto& i : s) {
			zz = logplusexp(zz, i.lw); // NOTE: This may be slightly imperfect with our approximations to lse, but it should not matter much
			if(zz >= r) return i.x;			
		}
		assert(0 && "*** Should not get here");
	}
	
};
