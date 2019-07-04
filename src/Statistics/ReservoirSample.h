#pragma once

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
	std::mutex lock;		

public:

	ReservoirSample(size_t n, bool u=false) : reservoir_size(n), unique(u), N(0) {
	}
	
	ReservoirSample(bool u=false) : reservoir_size(1000), unique(u), N(0) {
	}
	
	void set_size(const size_t s) const {
		reservoir_size = s;
	}
	
	size_t size() const {
		// How many am I currently storing?
		return s.size();
	}
	
	T max(){ return *vals.rbegin(); }
	T min(){ return *vals.begin(); }
	
	void add(T x) {
		std::lock_guard guard(lock);
		
		if((!unique) || vals.find(x) == vals.end()) {
		
			double r = uniform(rng);
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
			
			assert( (!unique) or s.size() == vals.size());
		}
		
		++N;
		
	}
	void operator<<(T x) {	add(x);}
	
	// Return a sample from my vals (e.g. a sample of the samples I happen to have saved)
	T sample() {
		std::lock_guard guard(lock);
		if(N == 0) return NaN;
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
