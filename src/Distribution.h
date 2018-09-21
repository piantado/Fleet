#ifndef DISTRIBUTION_H
#define DISTRIBUTION_H


// A distribution extends a map to sore elments of T mapped to their log probabilities

template<typename T>
class Distribution : std::map<T,double> { // well we're not supposed to inherit from stl, but what can you do? 
	// A distribution stores a map from elements of tyep T ot *log* probabilities
		
	void addmass(T& x, double lp) {
		if(this->count(x)) {
			(*this)[x] = logplusexp((*this)[x], lp);
		}
		else {
			(*this)[x] = lp;
		}
	}
	
};



#endif // DISTRIBUTION_H
