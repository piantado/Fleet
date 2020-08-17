#pragma once

#include "Numerics.h"

// turn off warnings for the rest of this
#pragma GCC system_header

// This is just a header to include Eigen, but preven us from warning about it. 
#include <cmath>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <unsupported/Eigen/SpecialFunctions>
#include "IO.h"

using Vector = Eigen::VectorXf; // set these and precision
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

double logsumexp(const Vector& v) {
	double mx = v.maxCoeff();
	//CERR v.transpose() TAB mx << "\n\n\n" ENDL;
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
