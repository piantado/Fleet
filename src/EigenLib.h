#pragma once

// turn off warnings for the rest of this
#pragma GCC system_header

// This is just a header to include Eigen, but preven us from warning about it. 
#include <cmath>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <unsupported/Eigen/SpecialFunctions>

using Vector = Eigen::VectorXf; // set these and precision
using Matrix = Eigen::MatrixXf; 

double logsumexp(const Vector& v) {
	
//	double mx = -infinity;
//	for(int i=0;i<v.size();i++) {
//		if(v(i) > mx) mx = v(i);
//	}
//	
//	double sm = 0.0;
//	for(int i=0;i<v.size();i++) {
//		sm += exp(v(i)-mx);
//	}
//	return log(sm)+mx;

	double mx = v.maxCoeff();
//	auto   o = Vector::Ones(v.size());
	
	return log((v.array()-mx).array().exp().sum()) + mx;
}

Vector lognormalize(const Vector& v) {
	return v - Vector::Ones(v.size())*logsumexp(v);
}

Vector eigenslice(const Vector& v, const size_t offset, const size_t len) {
	Vector out(len);
	for(size_t i=0;i<len;i++) {
		out(i) = v[i+offset];
	}
	return out;
}
