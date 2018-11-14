#ifndef IO_H
#define IO_H

#include<iostream>
#include<algorithm>

/* Handy functions for IO */


void print(std::ostream& out, Program& ops) {
	for(auto a: ops) {
		out << a << " ";
	}
	out << std::endl;
}

void print(std::ostream& out, std::map<std::string,double> x) {
	
	// put the strings into a vector
	std::vector<std::string> v;
	double Z = -infinity; // add up the mass
	for(auto a : x){ 
		v.push_back(a.first); 
		Z = logplusexp(Z, a.second);
	}
	
	// sort them to be increasing
	std::sort(v.begin(), v.end(), [x](std::string a, std::string b) { return x.at(a) > x.at(b); });
	
	
	out << "{";
	for(size_t i=0;i<v.size();i++){
		out << "'" << v[i] << "'" << ":" << x[v[i]];
		if(i < v.size()-1) { out << ", "; }
	}
	out << "} [Z=" << Z << "]";
}

template<typename T>
void print(T x) {
	print(std::cout, x);
}


#endif