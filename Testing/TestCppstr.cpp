// TestCppstr: cppstr's str() (to string) and str_to<T>() (from string), both directions.
//
//  1. Exact output format of str() for every supported kind of value (the format is a
//     contract: data files and Fleet's serialization depend on it).
//  2. Randomized round trips  str_to<T>(str(x)) == x  over scalars and deeply nested
//     containers, within the documented limits (no structural characters ,:[]{}<> inside
//     strings, no lone empty string as the only element of a vector/deque/set/multiset,
//     no subnormal floats). Floats must round-trip BIT-exactly (including -0.0 and inf).
//  3. The alternative input formats str_to documents (unbracketed lists, pair "a:b", ...).
//  4. Error behavior: wrong sizes, bad brackets, out-of-range narrowing.
//
// Random values come from a private std::mt19937 so this test is independent of Fleet's RNG.

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "cppstr.h"
#include "TestHarness.h"

using namespace TestHarness;

std::mt19937_64 rng;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Traits
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T, template<typename...> class R> struct is_spec : std::false_type {};
template<template<typename...> class R, typename... A> struct is_spec<R<A...>, R> : std::true_type {};
template<typename T> struct is_arr : std::false_type {};
template<typename T, size_t N> struct is_arr<std::array<T,N>> : std::true_type {};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Random value generation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// every printable ASCII character except the structural ones , : [ ] { } < >
const std::string SAFE = [] {
	std::string s;
	for(char c=32; c<127; c++) if(std::string(",:[]{}<>").find(c) == std::string::npos) s += c;
	return s;
}();

size_t randidx(size_t n) { return std::uniform_int_distribution<size_t>(0, n-1)(rng); }

template<typename F>
F random_float() {
	using U = std::conditional_t<sizeof(F)==4, uint32_t, uint64_t>;
	// mostly random bit patterns (covers the whole exponent range), sometimes special values
	if(randidx(10) == 0) {
		const std::vector<F> special = {F(0), -F(0), F(1), F(-1), std::numeric_limits<F>::infinity(), -std::numeric_limits<F>::infinity(),
		                                std::numeric_limits<F>::max(), std::numeric_limits<F>::lowest(), std::numeric_limits<F>::min(),
		                                -std::numeric_limits<F>::min(), std::numeric_limits<F>::epsilon(), F(0.1), F(1)/F(3), F(1e300 > std::numeric_limits<F>::max() ? 1e30 : 1e300)};
		return special[randidx(special.size())];
	}
	while(true) {
		U bits = (U)rng();
		F x; std::memcpy(&x, &bits, sizeof(F));
		if(std::isnan(x)) continue;
		if(x != 0 and std::fpclassify(x) == FP_SUBNORMAL) continue; // documented: subnormals do not parse
		return x;
	}
}

long double random_long_double() {
	// long double has padding bytes, so build values arithmetically
	if(randidx(10) == 0) return std::vector<long double>{0.0L, -0.0L, 1.0L, std::numeric_limits<long double>::max(), std::numeric_limits<long double>::min(),
	                                                  std::numeric_limits<long double>::infinity(), -std::numeric_limits<long double>::infinity()}[randidx(7)];
	const long double m = std::uniform_real_distribution<long double>(-1, 1)(rng);
	return std::ldexp(m, (int)randidx(32000) - 16000);
}

template<typename T> T gen(int depth);

std::string gen_string() {
	std::string s;
	const size_t n = randidx(9);
	for(size_t i=0;i<n;i++) s += SAFE[randidx(SAFE.size())];
	return s;
}

// documented exception: an element whose str() is "" cannot be the ONLY element of a
// vector/deque/set/multiset, because "[]" is read back as an empty container
template<typename E>
bool ambiguous_single(const E& e) { return str(e).empty(); }

template<typename T>
T gen(int depth) {
	const size_t maxlen = depth > 2 ? 2 : 5;
	if constexpr (std::is_same_v<T, std::string>) {
		return gen_string();
	}
	else if constexpr (std::is_same_v<T, bool>) {
		return randidx(2) == 1;
	}
	else if constexpr (std::is_same_v<T, char>) {
		return SAFE[randidx(SAFE.size())];
	}
	else if constexpr (std::is_same_v<T, float> or std::is_same_v<T, double>) {
		return random_float<T>();
	}
	else if constexpr (std::is_same_v<T, long double>) {
		return random_long_double();
	}
	else if constexpr (std::is_integral_v<T>) {
		if(randidx(5) == 0) return std::vector<T>{std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), T(0), T(1), T(std::is_signed_v<T> ? -1 : 2)}[randidx(5)];
		return (T)rng();
	}
	else if constexpr (is_spec<T,std::vector>::value or is_spec<T,std::deque>::value) {
		T v; const size_t n = randidx(maxlen+1);
		for(size_t i=0;i<n;i++) v.push_back(gen<typename T::value_type>(depth+1));
		if(v.size() == 1 and ambiguous_single(v[0])) v.clear();
		return v;
	}
	else if constexpr (is_spec<T,std::set>::value or is_spec<T,std::multiset>::value) {
		T v; const size_t n = randidx(maxlen+1);
		for(size_t i=0;i<n;i++) v.insert(gen<typename T::key_type>(depth+1));
		if(v.size() == 1 and ambiguous_single(*v.begin())) v.clear();
		return v;
	}
	else if constexpr (is_spec<T,std::map>::value) {
		T m; const size_t n = randidx(maxlen+1);
		for(size_t i=0;i<n;i++) m[gen<typename T::key_type>(depth+1)] = gen<typename T::mapped_type>(depth+1);
		return m;
	}
	else if constexpr (is_spec<T,std::pair>::value) {
		return T{gen<typename T::first_type>(depth+1), gen<typename T::second_type>(depth+1)};
	}
	else if constexpr (is_spec<T,std::tuple>::value) {
		return [&]<size_t... I>(std::index_sequence<I...>) {
			return T{ gen<std::tuple_element_t<I,T>>(depth+1)... };
		}(std::make_index_sequence<std::tuple_size_v<T>>{});
	}
	else if constexpr (is_arr<T>::value) {
		T a; for(auto& x : a) x = gen<typename T::value_type>(depth+1);
		return a;
	}
	else {
		static_assert(sizeof(T) == 0, "no generator");
	}
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Exact equality (floats compared bitwise so that -0.0 != 0.0)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename T>
bool same(const T& a, const T& b) {
	if constexpr (std::is_floating_point_v<T>) {
		return a == b and std::signbit(a) == std::signbit(b);
	}
	else if constexpr (is_spec<T,std::map>::value) {
		if(a.size() != b.size()) return false;
		auto i = a.begin(); auto j = b.begin();
		for(; i != a.end(); ++i, ++j) if(not same(i->first, j->first) or not same(i->second, j->second)) return false;
		return true;
	}
	else if constexpr (is_spec<T,std::pair>::value) {
		return same(a.first, b.first) and same(a.second, b.second);
	}
	else if constexpr (is_spec<T,std::tuple>::value) {
		return [&]<size_t... I>(std::index_sequence<I...>) { return (same(std::get<I>(a), std::get<I>(b)) and ...); }(std::make_index_sequence<std::tuple_size_v<T>>{});
	}
	else if constexpr (is_spec<T,std::vector>::value or is_spec<T,std::deque>::value or is_spec<T,std::set>::value or
	                   is_spec<T,std::multiset>::value or is_arr<T>::value) {
		if(a.size() != b.size()) return false;
		auto i = a.begin(); auto j = b.begin();
		for(; i != a.end(); ++i, ++j) if(not same(*i, *j)) return false;
		return true;
	}
	else {
		return a == b;
	}
}

template<typename T>
void roundtrip(const std::string& tname, int reps=3000) {
	int bad = 0;
	for(int r=0; r<reps; r++) {
		const T x = gen<T>(0);
		const std::string s = str(x);
		bool ok = false;
		std::string err;
		try {
			const T y = str_to<T>(s);
			ok = same(x, y);
			if(not ok) err = "parsed back as " + str(y);
			// and str must be a fixed point: str(str_to(str(x))) == str(x)
			if(ok and str(y) != s) { ok = false; err = "str(str_to(s)) = " + str(y); }
		} catch(const std::exception& e) {
			err = std::string("threw ") + e.what();
		}
		if(not ok and bad++ < 5) {
			CHECK(false, "round trip of " << tname << " failed for " << s << " : " << err);
		}
	}
	CHECK(bad == 0, tname << ": " << bad << " of " << reps << " random values did not round trip");
}

#define RT(...) roundtrip<__VA_ARGS__>(#__VA_ARGS__)

int main() {
	test_name = "Cppstr";
	rng.seed(seed());

	// ==================================================================================
	section("str(): exact formats");
	CHECK(str(42) == "42", "int");
	CHECK(str(-7L) == "-7", "long");
	CHECK(str(18446744073709551615ULL) == "18446744073709551615", "unsigned long long max");
	CHECK(str((int8_t)-5) == "-5" and str((uint8_t)200) == "200", "int8_t/uint8_t print as numbers, not characters");
	CHECK(str('a') == "a", "char prints as a character");
	CHECK(str(true) == "1" and str(false) == "0", "bool prints as 1/0");
	CHECK(str(0.1) == "0.1", "double uses the shortest round-trip form: " << str(0.1));
	CHECK(str(1.0/3.0) == "0.3333333333333333", "1/3: " << str(1.0/3.0));
	CHECK(str(1e100) == "1e+100", "1e100: " << str(1e100));
	CHECK(str(-0.0) == "-0", "-0.0: " << str(-0.0));
	CHECK(str(std::numeric_limits<double>::infinity()) == "inf" and str(-std::numeric_limits<double>::infinity()) == "-inf", "infinities");
	CHECK(str(0.1f) == "0.1", "float uses the shortest FLOAT form (not the double expansion of 0.1f): " << str(0.1f));
	CHECK(str(std::string("hi there")) == "hi there", "std::string is unchanged");
	CHECK(str("cstr") == "cstr", "C string");
	CHECK(str(std::string_view("sv")) == "sv", "string_view");
	CHECK(str((const char*)nullptr) == "nullptr" and str(nullptr) == "nullptr", "null pointers");
	CHECK(str(std::vector<int>{1,2,3}) == "[1,2,3]", "vector");
	CHECK(str(std::vector<int>{}) == "[]", "empty vector");
	CHECK(str(std::vector<int>{1,2}, ";") == "[1;2]", "vector with separator");
	CHECK(str(std::deque<int>{4,5}) == "[4,5]", "deque");
	CHECK(str(std::set<int>{3,1,2}) == "{1,2,3}", "set is sorted");
	CHECK(str(std::multiset<int>{2,1,2}) == "{1,2,2}", "multiset");
	CHECK(str(std::map<int,std::string>{{2,"b"},{1,"a"}}) == "{1:a,2:b}", "map");
	CHECK(str(std::pair<int,double>{1,2.5}) == "<1,2.5>", "pair");
	CHECK(str(std::tuple<int,std::string,char>{1,"x",'c'}) == "<1,x,c>", "tuple");
	CHECK(str(std::tuple<>{}) == "<>", "empty tuple");
	CHECK(str(std::array<int,3>{7,8,9}) == "<7,8,9>", "array");
	CHECK(str(std::vector<std::vector<int>>{{1},{},{2,3}}) == "[[1],[],[2,3]]", "nested vector");
	CHECK(str(std::map<std::string,std::vector<int>>{{"k",{1,2}}}) == "{k:[1,2]}", "map of vector");
	CHECK(str(std::atomic<int>(5)) == "5", "atomic");
	CHECK(str(", ", 1, "a", 2.5) == "1, a, 2.5", "str(sep, args...)");
	CHECK(str("-", std::string("x")) == "x", "str(sep, one arg)");
	struct HasString { std::string string() const { return "custom"; } };
	CHECK(str(HasString{}) == "custom", "objects with .string()");
	CHECK(print_line(1, "a", 2.5) == "1\ta\t2.5\n", "print_line uses tabs and one newline");

	// ==================================================================================
	section("split");
	CHECK(split("", ':').empty(), "split(\"\") -> []");
	CHECK((split("a", ':') == std::deque<std::string>{"a"}), "split(a)");
	CHECK((split("a:", ':') == std::deque<std::string>{"a",""}), "split(a:)");
	CHECK((split(":", ':') == std::deque<std::string>{"",""}), "split(:)");
	CHECK((split(":a", ':') == std::deque<std::string>{"","a"}), "split(:a)");
	CHECK((split("a::b", ':') == std::deque<std::string>{"a","","b"}), "split(a::b)");
	for(int r=0;r<2000;r++) {
		std::string s; const size_t n = randidx(12);
		for(size_t i=0;i<n;i++) s += (randidx(3)==0 ? ':' : 'a'+(char)randidx(3));
		auto parts = split(s, ':');
		size_t ndelim = std::count(s.begin(), s.end(), ':');
		std::string joined; for(size_t i=0;i<parts.size();i++) joined += (i ? ":" : "") + parts[i];
		CHECK(joined == s and (s.empty() ? parts.empty() : parts.size() == ndelim+1), "split invariant on " << s);
	}
	CHECK((split<3>("a,b,c", ',') == std::array<std::string,3>{"a","b","c"}), "split<3>");
	CHECK(throws([]{ (void)split<3>("a,b", ','); }), "split<N> wrong count throws");
	bool right_type = false;
	try { (void)split<2>("a", ','); } catch(const StringSplitException&) { right_type = true; } catch(...) {}
	CHECK(right_type, "split<N> throws StringSplitException");

	// ==================================================================================
	section("round trips: scalars");
	RT(int); RT(long); RT(long long); RT(short);
	RT(unsigned int); RT(unsigned long); RT(unsigned long long); RT(unsigned short);
	RT(int8_t); RT(uint8_t); RT(char); RT(bool);
	roundtrip<float>("float", 50000);
	roundtrip<double>("double", 50000);
	RT(long double);
	RT(std::string);
	CHECK(std::signbit(str_to<double>(str(-0.0))), "-0.0 keeps its sign");

	// ==================================================================================
	section("round trips: flat containers");
	RT(std::vector<int>); RT(std::vector<double>); RT(std::vector<std::string>); RT(std::vector<char>); RT(std::vector<bool>);
	RT(std::deque<long>); RT(std::deque<std::string>);
	RT(std::set<int>); RT(std::set<std::string>); RT(std::multiset<double>); RT(std::multiset<std::string>);
	RT(std::map<int,std::string>); RT(std::map<std::string,double>); RT(std::map<std::string,std::string>);
	RT(std::pair<int,double>); RT(std::pair<std::string,std::string>); RT(std::pair<double,int>);
	RT(std::tuple<int,std::string,double>); RT(std::tuple<std::string>); RT(std::tuple<std::string,std::string>); RT(std::tuple<>);
	RT(std::array<double,3>); RT(std::array<std::string,2>); RT(std::array<std::string,1>); RT(std::array<int,0>);

	// ==================================================================================
	section("round trips: nested containers");
	RT(std::vector<std::vector<int>>);
	RT(std::vector<std::vector<std::string>>);
	RT(std::vector<std::vector<std::vector<double>>>);
	RT(std::map<int, std::vector<double>>);
	RT(std::map<std::string, std::map<int,int>>);
	RT(std::vector<std::pair<int,std::string>>);
	RT(std::set<std::pair<int,int>>);
	RT(std::pair<std::vector<int>, std::map<std::string,int>>);
	RT(std::tuple<std::vector<int>, std::set<std::string>, std::pair<int,int>>);
	RT(std::vector<std::tuple<int,double,std::string>>);
	RT(std::array<std::vector<int>,2>);
	RT(std::vector<std::map<int,std::vector<std::string>>>);
	RT(std::deque<std::set<double>>);
	RT(std::pair<std::pair<int,int>,std::pair<std::string,std::string>>);
	RT(std::map<std::pair<int,int>,std::vector<int>>);
	RT(std::vector<std::array<int,3>>);
	RT(std::vector<std::tuple<std::string>>);
	RT(std::map<std::string, std::pair<std::string, std::vector<std::string>>>);
	RT(std::tuple<std::map<int,int>, std::tuple<std::string, std::vector<std::pair<int,int>>>>);

	// ==================================================================================
	section("str_to: alternative input formats (documented)");
	CHECK((str_to<std::vector<int>>("1,2,3") == std::vector<int>{1,2,3}), "vector without brackets");
	CHECK((str_to<std::set<int>>("3,1") == std::set<int>{1,3}), "set without braces");
	CHECK((str_to<std::map<std::string,int>>("a:1,b:2") == std::map<std::string,int>{{"a",1},{"b",2}}), "map without braces");
	CHECK((str_to<std::map<int,int>>("{1:2,1:3}") == std::map<int,int>{{1,3}}), "map: later duplicate keys overwrite");
	CHECK((str_to<std::pair<std::string,int>>("x:5") == std::pair<std::string,int>{"x",5}), "pair as a:b");
	CHECK((str_to<std::pair<int,int>>("<4,5>") == std::pair<int,int>{4,5}), "pair as <a,b>");
	CHECK((str_to<std::tuple<int,int>>("4,5") == std::tuple<int,int>{4,5}), "tuple without angle brackets");
	CHECK((str_to<std::vector<std::string>>("a;b", ';') == std::vector<std::string>{"a","b"}), "custom top-level separator");
	CHECK((str_to<std::vector<int>>("[]").empty()), "[] is empty");
	CHECK((str_to<std::vector<std::string>>("[]").empty()), "documented: [] is read as empty, never as {\"\"}");
	CHECK(str_to<bool>("1") and str_to<bool>("true") and str_to<bool>("True") and str_to<bool>("t") and not str_to<bool>("0") and not str_to<bool>("false"), "bool");
	CHECK(str_to<int>(" 12") == 12, "documented leniency: leading whitespace");
	CHECK(str_to<int>("3.7") == 3, "documented leniency: trailing junk ignored");

	// ==================================================================================
	section("str_to: errors");
	CHECK(throws([]{ (void)str_to<int>("abc"); }), "non-number int");
	CHECK(throws([]{ (void)str_to<double>(""); }), "empty double");
	CHECK(throws([]{ (void)str_to<char>("ab"); }), "char needs exactly one character");
	CHECK(throws([]{ (void)str_to<char>(""); }), "char from empty");
	CHECK(throws([]{ (void)str_to<uint8_t>("256"); }), "uint8_t out of range");
	CHECK(throws([]{ (void)str_to<int8_t>("-129"); }), "int8_t out of range");
	CHECK(throws([]{ (void)str_to<short>("40000"); }), "short out of range");
	CHECK(throws([]{ (void)str_to<unsigned short>("-1"); }), "unsigned short from -1");
	CHECK(throws([]{ (void)str_to<unsigned int>("-1"); }), "unsigned int from -1");
	CHECK(throws([]{ (void)str_to<unsigned long>("-1"); }),
	      "str_to<unsigned long>(\"-1\") silently returns " << str_to<unsigned long>("-1")
	      << " (std::stoul accepts a minus sign and wraps), while unsigned int/short throw out_of_range for the same input");
	CHECK(throws([]{ (void)str_to<unsigned long long>("-1"); }),
	      "str_to<unsigned long long>(\"-1\") silently wraps to " << str_to<unsigned long long>("-1"));
	CHECK(throws([]{ (void)str_to<int>("99999999999"); }), "int overflow");
	CHECK(throws([]{ (void)str_to<double>("1e-310"); }), "documented: subnormal doubles throw");
	CHECK(throws([]{ (void)str_to<std::pair<int,int>>("1"); }), "pair with one element");
	CHECK(throws([]{ (void)str_to<std::pair<int,int>>("<1,2,3>"); }), "pair with three elements");
	CHECK(throws([]{ (void)str_to<std::tuple<int,int>>("<1>"); }), "tuple wrong size");
	CHECK(throws([]{ (void)str_to<std::array<int,2>>("<1,2,3>"); }), "array wrong size");
	CHECK(throws([]{ (void)str_to<std::map<int,int>>("{1}"); }), "map entry without colon");
	CHECK(throws([]{ (void)str_to<std::vector<std::vector<int>>>("[[1,2],[3]"); }), "unbalanced brackets (nested)");
	CHECK(throws([]{ (void)str_to<std::vector<std::vector<int>>>("[[1,2},[3]]"); }), "mismatched brackets (nested)");
	CHECK(throws([]{ (void)str_to<std::vector<int>>("[1,,2]"); }), "empty int element");

	return finish();
}
