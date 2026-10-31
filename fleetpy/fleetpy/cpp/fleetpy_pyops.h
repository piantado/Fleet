// fleetpy: Python semantics for transpiled (tier-2) code.
//
// Each helper implements one Python operation exactly, or makes the call *undefined* by
// throwing Fleet's VMSRuntimeError where Python would raise (or where int64 cannot hold the
// result). fleetpy's transpiler emits calls to these and nothing else for any operation
// whose C++ meaning differs from Python's. check/reference.py is the executable Python
// statement of the same semantics, and every build differential-tests the two.
//
// Include after Fleet's headers (it needs VMSRuntimeError).
//
// Python evaluates a*b + c with two roundings; under -march=native GCC would fuse it into
// one FMA (even in ISO C++ mode). Transpiled code must match Python bit for bit, so these
// helpers -- like all transpiled code -- are compiled without contraction.
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#pragma GCC push_options
#pragma GCC optimize ("fp-contract=off")

namespace fp {
	[[noreturn]] inline void undefined() { throw VMSRuntimeError(); }

	template<typename T> inline T narrow(int64_t v) {
		if constexpr (std::is_same_v<T, int64_t>) { return v; }
		else {
			if(v < (int64_t)std::numeric_limits<T>::min() or v > (int64_t)std::numeric_limits<T>::max()) undefined();
			return (T)v;
		}
	}
	inline int64_t add(int64_t a, int64_t b) { int64_t r; if(__builtin_add_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t sub(int64_t a, int64_t b) { int64_t r; if(__builtin_sub_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t mul(int64_t a, int64_t b) { int64_t r; if(__builtin_mul_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t neg(int64_t a) { if(a == std::numeric_limits<int64_t>::min()) undefined(); return -a; }
	inline int64_t iabs(int64_t a) { return a < 0 ? neg(a) : a; }
	inline int64_t floordiv(int64_t a, int64_t b) {
		if(b == 0) undefined();
		if(a == std::numeric_limits<int64_t>::min() and b == -1) undefined();
		int64_t q = a / b;
		if((a % b != 0) and ((a < 0) != (b < 0))) q -= 1;
		return q;
	}
	inline int64_t mod(int64_t a, int64_t b) {
		if(b == 0) undefined();
		if(b == -1) return 0; // avoids INT64_MIN % -1 (UB); Python gives 0
		int64_t r = a % b;
		if(r != 0 and ((r < 0) != (b < 0))) r += b;
		return r;
	}
	inline int64_t ipow(int64_t a, int64_t k) { // k >= 0, checked by the transpiler
		int64_t r = 1;
		for(int64_t i = 0; i < k; i++) r = mul(r, a);
		return r;
	}
	inline double fdiv(double a, double b) { if(b == 0.0) undefined(); return a / b; }
	inline double idiv(int64_t a, int64_t b) {
		if(b == 0) undefined();
		const int64_t L = (int64_t)1 << 53;
		if(a > L or a < -L or b > L or b < -L) undefined();
		return (double)a / (double)b;
	}
	inline double fpow(double a, double b) {
		if(a == 0.0 and b < 0.0) undefined();                       // ZeroDivisionError
		double r = std::pow(a, b);
		if(std::isinf(r) and std::isfinite(a) and std::isfinite(b)) undefined(); // OverflowError
		if(std::isnan(r) and not std::isnan(a) and not std::isnan(b)) undefined(); // complex result
		return r;
	}
	template<typename... A> inline double mcheck(double r, A... in) {
		const bool anynan = (std::isnan((double)in) or ...);
		const bool allfinite = (std::isfinite((double)in) and ...);
		if(std::isnan(r) and not anynan) undefined();
		if(std::isinf(r) and allfinite) undefined();
		return r;
	}
	inline int64_t to_int(double x) { // Python int(float): truncation; nan/inf/out of range -> undefined
		if(std::isnan(x) or std::isinf(x)) undefined();
		double t = std::trunc(x);
		if(t < -9223372036854775808.0 or t >= 9223372036854775808.0) undefined();
		return (int64_t)t;
	}
	inline size_t cp_len(const std::string& s) {
		size_t n = 0;
		for(unsigned char c : s) n += ((c & 0xC0) != 0x80);
		return n;
	}
	inline std::string cp_index(const std::string& s, int64_t i) {
		int64_t n = (int64_t)cp_len(s);
		if(i < 0) i += n;
		if(i < 0 or i >= n) undefined();
		size_t k = 0; int64_t cp = -1; size_t start = 0;
		for(; k < s.size(); k++) {
			if(((unsigned char)s[k] & 0xC0) != 0x80) { cp++; if(cp == i) { start = k; break; } }
		}
		size_t end = start + 1;
		while(end < s.size() and (((unsigned char)s[end] & 0xC0) == 0x80)) end++;
		return s.substr(start, end - start);
	}
	template<typename V> inline const typename V::value_type& index(const V& v, int64_t i) {
		int64_t n = (int64_t)v.size();
		if(i < 0) i += n;
		if(i < 0 or i >= n) undefined();
		return v[(size_t)i];
	}
	template<typename V> inline V concat(const V& a, const V& b) { V r = a; r.insert(r.end(), b.begin(), b.end()); return r; }
	// Python slice bounds: negative indices count from the end, then clamp to [0, n]
	inline int64_t slice_bound(bool has, int64_t i, int64_t n, int64_t dflt) {
		if(not has) return dflt;
		if(i < 0) { i += n; if(i < 0) i = 0; }
		else if(i > n) i = n;
		return i;
	}
	template<typename V> inline V slice(const V& v, bool hl, int64_t lo, bool hh, int64_t hi) {
		const int64_t n = (int64_t)v.size();
		lo = slice_bound(hl, lo, n, 0); hi = slice_bound(hh, hi, n, n);
		if(hi <= lo) return V{};
		return V(v.begin() + lo, v.begin() + hi);
	}
	inline size_t cp_offset(const std::string& s, int64_t cp) { // byte offset of code point cp (or size())
		int64_t k = -1;
		for(size_t b = 0; b < s.size(); b++) {
			if(((unsigned char)s[b] & 0xC0) != 0x80) { k++; if(k == cp) return b; }
		}
		return s.size();
	}
	inline std::string cp_slice(const std::string& s, bool hl, int64_t lo, bool hh, int64_t hi) {
		const int64_t n = (int64_t)cp_len(s);
		lo = slice_bound(hl, lo, n, 0); hi = slice_bound(hh, hi, n, n);
		if(hi <= lo) return std::string();
		const size_t a = cp_offset(s, lo), b = cp_offset(s, hi);
		return s.substr(a, b - a);
	}
	inline std::string str(char c) { return std::string(1, c); }
	inline const std::string& str(const std::string& x) { return x; }
	// Python's << and >> on int64 values: negative shift counts raise; results must fit int64
	inline int64_t shl(int64_t a, int64_t n) {
		if(n < 0) undefined();
		if(a == 0) return 0;
		if(n >= 64) undefined();
		const __int128 r = (__int128)a << n;
		if(r > (__int128)std::numeric_limits<int64_t>::max() or r < (__int128)std::numeric_limits<int64_t>::min()) undefined();
		return (int64_t)r;
	}
	inline int64_t shr(int64_t a, int64_t n) {
		if(n < 0) undefined();
		if(n >= 64) return a < 0 ? -1 : 0;
		return a >> n;  // arithmetic shift (floor), as Python
	}
	inline uint64_t uabs(int64_t a) { return a < 0 ? (uint64_t)0 - (uint64_t)a : (uint64_t)a; }
	inline int64_t bit_count(int64_t a)  { return __builtin_popcountll(uabs(a)); }
	inline int64_t bit_length(int64_t a) { const uint64_t u = uabs(a); return u == 0 ? 0 : 64 - __builtin_clzll(u); }
	template<typename T> inline T minv(T a, T b) { return (b < a) ? b : a; } // Python min: first wins ties
	template<typename T> inline T maxv(T a, T b) { return (b > a) ? b : a; } // Python max: first wins ties
}

#pragma GCC pop_options
