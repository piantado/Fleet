#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Compute polynomial degrees
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


/* This class stores the degree of a polynomial and a bool that indicates whether
 * its a degree or a constant (which is somemtimes used in computing the degree). When
 * we end up with a non-polynomial, we return NaN */

class polydeg {
public:
    double value; //
    bool   is_const; //if true, value is a constant, otherwise it is an exponent on x
    polydeg(double v, bool b) : value(v), is_const(b) {
    }
};


polydeg get_polynomial_degree_rec(Node* n) {
	
	std::string fmt = n->rule->format;
	
    if(fmt == "(%s+%s)") {
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        polydeg v2 = get_polynomial_degree_rec(n->child[1]);
        if(v1.is_const && v2.is_const) return polydeg(v1.value+v2.value, true); // if both consts, then return their value
        else if (v1.is_const) return v2;
        else if (v2.is_const) return v1;
        else return polydeg(std::max(v1.value,v2.value), false);
    }
    else if(fmt == "(%s*%s)") {
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        polydeg v2 = get_polynomial_degree_rec(n->child[1]);
        if(v1.is_const && v2.is_const) return polydeg(v1.value*v2.value,true); // if boths consts, then return their value
        else if(v1.is_const) return v2;
        else if(v2.is_const) return v1;
        else return polydeg(v1.value+v2.value,false); // both are powers so they add
    }
    else if(fmt == "(%s/%s)") {
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        polydeg v2 = get_polynomial_degree_rec(n->child[1]);
        if(v1.is_const && v2.is_const) return polydeg(v1.value/v2.value,true); // if boths consts, then return their value
        else if(v1.is_const) return polydeg(NaN,false); // don't count positive powers
        else if(v2.is_const) return v1; // v1 is an exponent, constant doesn't matter
        return polydeg(NaN,false); //NOTE then (x...)/(x...) isn't a polynomial
    }
    else if(fmt == "(%s^%s)") {
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        polydeg v2 = get_polynomial_degree_rec(n->child[1]);
        if(v1.is_const && v2.is_const) return polydeg(pow(v1.value,v2.value),true); // if both constants, take their power
        else if(v1.is_const) return polydeg(NaN,false); // 2.3 ^ x -- not a polynomial
        else if(v2.is_const) return polydeg(v1.value*v2.value,false); // x^{2.3} -- is a polynomial, so multiply the exponents
        return polydeg(NaN,false); // both are exponents but not a polynomial so forget it
    }
    else if(fmt == "(- %s)") {
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        return v1;
    }
    else if(fmt == "x") {
        return polydeg(1.0,false);
    }
	else if(fmt == "C") { 
        return polydeg(0.0, true);
    }
	else if(fmt == "1") {
        return polydeg(1.0, true);
    }
	else if(fmt == "log(%s)") { 
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        if(v1.is_const) return polydeg(log(v1.value), true);
        else return polydeg(NaN,false);
    }
	else if(fmt == "exp(%s)") { 
        polydeg v1 = get_polynomial_degree_rec(n->child[0]);
        if(v1.is_const) return polydeg(exp(v1.value), true);
        else return polydeg(NaN,false);
    }
	else {
		assert(0);
	}
}

double get_polynomial_degree(Node* n) {
    polydeg r = get_polynomial_degree_rec(n);
	return (r.is_const ? 0.0 : r.value);
}
