#ifndef IO_H
#define IO_H

#include<iostream>


/* Handy functions for IO */


void print(std::ostream& out, Program& ops) {
	for(auto a: ops) {
		out << a << " ";
	}
	out << std::endl;
}


template<typename T>
void print(T x) {
	print(std::cout, x);
}


#endif