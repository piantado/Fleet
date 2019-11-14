#ifndef MYPRIMITIVES_H
#define MYPRIMITIVES_H

#include <string>
#include <cmath>
#include <math.h>

#include "Numerics.h"
#include "Random.h"

namespace Model {
	
	typedef int word;
	typedef std::string set; 	/* This models sets as strings */
	typedef char objtype; // store this as that
	typedef std::tuple<set,word> X; 
	typedef unsigned long wmset; // just need an integerish type
	typedef float magnitude; 
	
	const double W = 0.3;
	
	const word U = -999;

	const size_t MAX_SET_SIZE = 20;
	
	bool match1to1(const wmset x, const set y) {
		return x == y.length();
	}
	
	// Just for now, mimicing some multi-set ops with ints
	set select(const set x) { return x.substr(0,1); } // just select the first

	set selectObj(const set x, objtype o) {  // actually needed to allow recursive form concisely
		if(x.find(o) != std::string::npos){
			return std::string(1,o); // must exist there
		}
		else {
			return set("");
		}
	} // just select the first


	set myunion(const set x, const set y) { 
		if(x.length() + y.length() < MAX_SET_SIZE){
			return x+y; 
		} 
		else { 
			return set("");
		}

	}

	set difference(const set x, const set y) { 
		std::string out = x;
		for(size_t yi=0;yi<y.length();yi++) {
			std::string s = y.substr(yi,1);
			size_t pos = out.find(s); // do I occur?
			if(pos != std::string::npos) {
				out.erase(pos,1); // remove that character
			}
		}
		return out;
	}
	
	set intersection(const set x, const set y) { 
		std::string out = "";
		for(size_t yi=0;yi<y.length();yi++) {
			std::string s = y.substr(yi,1);
			size_t pos = x.find(s); // do I occur?
			if(pos != std::string::npos) {
				out.append(s);
			}
		}
		return out;
	}

	// filter a set by a given objtype
	set filter(const set x, const objtype t) {
		std::string tstr = std::string(1,t); // a std::string to find
		std::string out = "";
		size_t pos = x.find(tstr);
		while (pos != std::string::npos) {
			out = out + tstr;
			pos = x.find(tstr,pos+1);
		}
		return out;
	}

	bool empty(const set x)     { return x.length() == 0; }
	bool singleton(const set x) { return x.length()==1; }
	bool doubleton(const set x) { return x.length()==2; }
	bool tripleton(const set x) { return x.length()==3; }

	double normcdf(const double x) { 
		return 0.5 * (1 + erf(x * M_SQRT1_2));
	}

	double ANSdiff(const double n1, const double n2) {  // SD of the weber value
		if(n1 == 0.0 && n2 == 0.0) return 0.0; // hmm is this right?
		
		return (n1-n2) / (W*sqrt(n1*n1+n2*n2));
	}
	
	double ANSzero(const double n1, const double n2) {
		// probability mass assigned to exactly 0 under weber scaling
		return exp(normal_lpdf(0, n1-n2, W*sqrt(n1*n1+n2*n2)));
	}
	
	
	double ANS_Eq(const set s, const set s2) {
		return ANSzero(s.length(), s2.length()); // we'll call differences w/in 1 SD equal 		
	}
	double ANS_Eq(const set s, const wmset s2) {
		return ANSzero(s.length(), s2); // we'll call differences w/in 1 SD equal 		
	}
	double ANS_Eq(const set s, const magnitude m) {
		return ANSzero(s.length(), m); // we'll call differences w/in 1 SD equal 		
	}

	double ANS_Lt(const set s, const set s2) {
		return 1.0-normcdf(ANSdiff(s.length(), s2.length())); // we'll call differences w/in 1 SD equal 		
	}
	double ANS_Lt(const set s, const wmset s2) {
		return 1.0-normcdf(ANSdiff(s.length(), s2)); // we'll call differences w/in 1 SD equal 		
	}
	double ANS_Lt(const set s, const magnitude m) {
		return 1.0-normcdf(ANSdiff(s.length(), m)); // we'll call differences w/in 1 SD equal 		
	}

	word next(const word w)   { 
		if(w >= 1) return w+1;
		else       return U;
	}
	word prev(const word w)   { 
		if(w >= 1) return w-1;
		else       return U;  
	}

}


#endif