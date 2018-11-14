#ifndef PRIMITIVES_H
#define PRIMITIVES_H

#include <math.h>
#include <algorithm>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Basic arithemtic
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

double add(const double x, const double y) { return x+y; }
double times(const double x, const double y) { return x*y; }
double div(const double x, const double y) { return (y==0.0 ? 0.0 : x/y); }
double neg(const double x) { return -x; }
double alt1(const double x) { return pow(-1,x); } // (-1)^x (as in sequences)
// pow, exp, log, etc. are already built in

long plus1(const long x) { return x+1; }
long minus1(const long x) { return x-1; }
long add_long(const long x, const long y) { return x+y; }
long times_long(const long x, const long y) { return x*y; }
long pow_long(const long x, const long y) { return pow(x,y); } // tODO: CLEAN UP
long div_long(const long x, const long y) { return (y==0.0 ? 0.0 : x/y); }
long neg_long(const long x) { return -x; }
long alt1_long(const long x) { return pow(-1,x); } // (-1)^x (as in sequences)
// pow, exp, log, etc. are already built in



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Basic logic
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

bool true_() { return true;}
bool false_(){ return false;}
bool and_(bool x, bool y) { return x&&y; }
bool or_(bool x, bool y) { return x||y; }
bool not_(bool x) { return !x; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Scheme operations
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

std::string emptystring_() { return std::string(""); }

std::string cons_(const std::string x, const std::string y) {
    std::string out = x;
    out.append(y);
    return out;
}


std::string car_(const std::string x) {
    if(x.empty()) return std::string("");
    else return std::string(1,x.at(0));
}

std::string cdr_(const std::string x) {
    if(x.empty()) return std::string("");
    else          return x.substr(1,std::string::npos);
}


std::string char2s_(const char c){
    return std::string(1,c);
}

bool empty_(const std::string x) {
    return x.empty();
}

bool equals_(const std::string x, const std::string y) {
    return x==y;
}

bool equals_(const char x, const char y) {
    return x==y;
}

bool eq_(double x, double y) { return x==y; }

bool lt_(double x, double y) { return x<y; }

/* Below are some std::string functiosn that are more abstract, higher-order */

std::string filter_(const std::string x, const char c){
    // give back all of x that match c
    return std::string(std::count(x.begin(), x.end(), c), c);
}

std::string repstrlen_(const std::string x, const std::string y){ // make y.length() repetitions of x
    // give back all of x that match c
    if(x.length()*y.length() > 1000) return "";
    
    std::string out = "";
    for(size_t i=0;i<y.length();i++) {
        out = out+x;
    }
    return out;
}


std::string replen_(const std::string x, const size_t n){ // make y.length() repetitions of x
    // give back all of x that match c
    if(x.length()*n > 1000) return "";
    
    std::string out = "";
    for(size_t i=0;i<n;i++) {
        out = out+x;
    }
    return out;
}

size_t count_(const std::string s, const char c) {
	size_t cnt = 0;
	for(auto a: s){
		if(a==c) ++cnt;
	}
	return cnt;
}


#endif