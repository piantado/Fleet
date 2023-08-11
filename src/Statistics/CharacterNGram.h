#pragma once 

#include <map>
#include <string>

/**
 * @class CharacterNGram
 * @author Steven Piantadosi
 * @date 03/06/23
 * @file CharacterNGram.h
 * @brief A simple ngram model over characters, usually used for computing baseline generalization/accuracy measures
 */

class CharacterNGram {
protected:
	int n; 
	std::map<std::string, size_t> count; 
	std::map<std::string, size_t> count_nm1; // for computing conditional 

public:
	
	CharacterNGram(size_t _n) : n(_n) {
		assert(n > 0);
	}
	
	void train(std::string s) {
		
		// let's see, in a string like abcdefg
		//                             0123456
		// we want to compute for a bigram P(b|a)
		for(size_t i=0;i<=s.size()-n;i++) {
			auto x = s.substr(i,n);
			auto y = s.substr(i,n-1);
			count[x]     = get(count,     x, 0) + 1;
			count_nm1[y] = get(count_nm1, y, 0) + 1;				
		}
	}
	
	void masked_train(const std::string& s, char mask) {
		// this is the same as train except that we skip *anything* containing msk 
		// within the n-gram
		
		for(size_t i=0;i<=s.size()-n;i++) {
			auto x = s.substr(i,n);
			if(not contains(x, mask)) {				
				auto y = s.substr(i,n-1);
//				print("TRAINING", n, x, y);
				count[x]     = get(count,     x, 0) + 1;
				count_nm1[y] = get(count_nm1, y, 0) + 1;				
			}
		}
	}
	
	virtual double probability(const std::string&) = 0; 
};

class AddLambdaSmoothedNGram : public CharacterNGram {
	
	int alphabet_size; // needed for smoothing
	double lambda; 

public:

	AddLambdaSmoothedNGram(int _n, int a, double l) : CharacterNGram(_n), alphabet_size(a), lambda(l) {
		
	}
	
	virtual double probability(const std::string& x) override {
		// probability of the LAST character of x, given the previous,
		// using add_l smoothing, with alphabet size a
		// NOTE: returns prob, not logprob!
//		print(n, "GETTING", QQ(x), get(count, x, 0), QQ(x.substr(0,x.size()-1)), get(count_nm1, x.substr(0,x.size()-1), 0));
		return (get(count, x, 0) + lambda) / (get(count_nm1, x.substr(0,x.size()-1), 0) + lambda * alphabet_size);				
	}
};