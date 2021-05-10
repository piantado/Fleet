#pragma once 

#include <vector>

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start, int len) {
	/**
	 * @brief Take a slice of a vector v starting at start of length len
	 * @param v
	 * @param start
	 * @param len
	 * @return 
	 */
	
	std::vector<T> out;
	if(len > 0) { // in case len=-1
		out.resize(len); 
		std::copy(v.begin() + start, v.begin()+start+len, out.begin());
	}
	return out;
}

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t start) {
	/**
	 * @brief Take a slice of a vector until its end
	 * @param v
	 * @param start
	 * @return 
	 */
		
	// just chop off the end 
	return slice(v, start, v.size()-start);
}

template<typename T>
void increment(std::vector<T>& v, size_t idx, T count=1) {
	// Increment and potentially increase my vector size if needed
	if(idx >= v.size()) {
		v.resize(idx+1,0);
	}
	
	v[idx] += count;
}

/**
 * @brief Glom here takes a vector of pointers to vectors. It searches through X to find if anything
 *        matches the beginning of v, and if so, it will append the additional elements, and use the 
 *        existing data. It then MODIFIES the v pointer (which is passed by reference). 
 * 		  If no match can be found, glom will append v into x. 
 * 
 * 		  Thus, at the end of this, v points to something in X, but preferentially re-uses data in X. This
 *        is especially useful when we're compiling incremental data for learning models. 
 * 
 * 		  NOTE: that sizes must be stored externally, since if a vector is stored in here, it may be
 *        appended to (so whatever is using X can't just compute the size)
 * 
 * 		  This is used primarily for GrammarHypothesis storage of MCMC data, where we want to load
 *        multiple datasets and share overlap whenever possible (so we don't duplicate MCMC sampling)
 *     
 * @param X
 * @param v
 * @return This returns a (possibly) new pointer for v. That is, it will either leave v unchanged
 *         or it will point to something in x
 */
template<typename T>
void glom(std::vector<std::vector<T>*>& X, std::vector<T>*& v) {
	
	// if v is a prefix of anything in x, then 
	// we can just use that. 
	for(auto& x : X) {
		if(is_prefix(*v, *x)) {
			delete v;
			v = x; // set V to the existing datset
			return; 
		}
	}
	
	// else check if v extends anything in X, and if it does, 
	// find the longest match and add the remaining elements
	// to the stuff stored in X
	size_t mx = 0;
	std::vector<T>* match = nullptr;
	for(auto& x : X) {
		if(is_prefix(*x, *v) and x->size() > mx) {
			mx = x->size();
			match = x;
		}
	}	
	
	// if anything matched in X
	if(match != nullptr) {
		
		// append the remaining elements to x
		for(size_t i=mx;i<v->size();i++) 
			match->push_back(v->at(i));
		
		// delete v since its not needed anymore
		delete v;
		
		// and we can use match
		v = match;
	}
	else {
		// else nothing matches so we have to just return v;
		X.push_back(v);
	}
	
	
}