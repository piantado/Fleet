#pragma once

#include "Numerics.h"

// turn off warnings for the rest of this
#pragma GCC system_header

// This is just a header to include Eigen, but preven us from warning about it. 
#include <cmath>
#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Eigenvalues> 
#include <eigen3/unsupported/Eigen/SpecialFunctions>
#include "IO.h"

// Define some useful names here 
using Vector = Eigen::VectorXf; 
using Matrix = Eigen::MatrixXf; 

// Woah this is WAAAY slower
//double logsumexp(const Vector& v) {
//	
//	double lse = -infinity;
//	for(int i=0;i<v.size();i++) {
//		lse = logplusexp((double)v(i), lse);
//	}
//	return lse;
//}

double logsumexp_eigen(const Vector& v) {
	double mx = v.maxCoeff();
	return log((v.array()-mx).array().exp().sum()) + mx;
}

Vector lognormalize(const Vector& v) {
	return v.array() - logsumexp_eigen(v);
}

Vector eigenslice(const Vector& v, const size_t offset, const size_t len) {
	Vector out(len);
	for(size_t i=0;i<len;i++) {
		out(i) = v[i+offset];
	}
	return out;
}

// And we define a macro so that other code in fleet can optionally include Eigen-compatible 
// operations.
// NOTE that this requires us to include EigenLib before we import anything else (e.g. grammar)
#define AM_I_USING_EIGEN 1
