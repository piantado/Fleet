#pragma once 

/* Some handy wrappers for the Eigen library */

#include <Eigen/Dense>

using Vector = Eigen::VectorXf; // set these and precision
using Matrix = Eigen::MatrixXf; 

double logsumexp(Vector v) {
	
	double mx = -infinity;
	for(int i=0;i<v.size();i++) {
		if(v(i) > mx) mx = v(i);
	}
	
	double sm = 0.0;
	for(int i=0;i<v.size();i++) {
		sm += exp(v(i)-mx);
	}
	return log(sm)+mx;
}

Vector lognormalize(Vector v) {
	return v - Vector::Ones(v.size())*logsumexp(v);
}
