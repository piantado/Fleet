/*
 * Tests for GeneralEnumeration: a bijection between integers and trees that is correct when some
 * nonterminals have finitely many trees, have no terminal rules, or are unproductive.
 *
 * Correctness is checked against an INDEPENDENT brute-force enumerator (all trees of each exact size,
 * built directly from the grammar's rules, with no use of the enumerator's analysis):
 *   - every brute-force tree t round-trips: toNode(toInteger(t)) == t, and distinct trees get
 *     distinct codes (so toInteger is injective and every tree has a code that decodes to it);
 *   - for a prefix of integers, toInteger(toNode(z)) == z and the decoded trees are distinct;
 *   - finite nonterminals: count() equals the brute-force count, toNode over [0,count) is exactly
 *     the brute-force set, and toNode(count) throws;
 *   - overflow is never silent: a code either round-trips or throws EnumerationOverflowException,
 *     and the pairing primitives agree with exact 128-bit arithmetic.
 *
 *   make && ./main            (prints ALL TESTS PASSED, or the failures)
 */

#include <set>
#include <map>
#include <random>
#include <functional>

#include "Grammar.h"
#include "Singleton.h"
#include "GeneralEnumeration.h"
#include "Fleet.h"

// Nonterminals (one per type):
//   int    infinite, has terminals; also has a rule with an unproductive child (bad) that must be skipped
//   bool   FINITE (14 trees):  T | F | lt(char,char) | z(char)
//   char   FINITE (3 trees):   '0' | '1' | '2'
//   double INFINITE with NO terminals: f(double) [highest p, sorts first] | h(bool,double) | g(int)
//          (BasicEnumeration decodes 0 here as f(f(f(...))) and overflows the stack)
//   long   INFINITE with NO terminals, minimal tree two levels down: v(long) | w(double)
//   float  UNPRODUCTIVE: u(float) only
class MyGrammar : public Grammar<int,int, int,bool,char,double,long,float>, public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add_terminal("1", 1, 1.0);
		add_terminal("2", 2, 0.5);
		add("(%s+%s)",      +[](int a, int b) -> int { return a+b; }, 1.0);
		add("s(%s)",        +[](int a) -> int { return a+1; }, 0.7);
		add("if(%s,%s,%s)", +[](bool c, int a, int b) -> int { return c ? a : b; }, 0.3);
		add("dig(%s)",      +[](char c) -> int { return c-'0'; }, 0.4);
		add("w2i(%s)",      +[](double x) -> int { return (int)x; }, 0.2);
		add("bad(%s)",      +[](float x) -> int { return (int)x; }, 0.05);

		add_terminal("T", true, 1.0);
		add_terminal("F", false, 1.0);
		add("lt(%s,%s)",    +[](char a, char b) -> bool { return a<b; }, 0.5);
		add("z(%s)",        +[](char a) -> bool { return a=='0'; }, 0.5);

		add_terminal("'0'", '0', 1.0);
		add_terminal("'1'", '1', 1.0);
		add_terminal("'2'", '2', 1.0);

		add("f(%s)",        +[](double x) -> double { return x; }, 2.0);
		add("h(%s,%s)",     +[](bool b, double x) -> double { return b ? x : -x; }, 1.5);
		add("g(%s)",        +[](int a) -> double { return a; }, 1.0);

		add("v(%s)",        +[](long x) -> long { return x; }, 3.0);
		add("w(%s)",        +[](double x) -> long { return (long)x; }, 1.0);

		add("u(%s)",        +[](float x) -> float { return x; }, 1.0);
	}
} grammar;

using Enum = GeneralEnumeration<MyGrammar>;
using idx_t = enumerationidx_t;

size_t nfail = 0;
#define CHECK(cond, ...) do { if(not (cond)) { nfail++; print("FAIL:", #cond, __VA_ARGS__); } } while(0)

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Independent brute force: all complete trees of nonterminal nt with exactly s nodes
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
std::map<std::pair<nonterminal_t,size_t>, std::vector<Node>> memo;

const std::vector<Node>& all_trees(const nonterminal_t nt, const size_t s) {
	auto key = std::make_pair(nt, s);
	if(memo.count(key)) return memo[key];
	std::vector<Node> out;
	if(s >= 1) {
		for(const Rule& r : grammar.rules[nt]) {
			if(r.N == 0) {
				if(s == 1) out.push_back(grammar.makeNode(&r));
				continue;
			}
			// distribute s-1 nodes among the N children (each >= 1), then take the cartesian product
			std::vector<size_t> sizes(r.N, 1);
			std::function<void(size_t,size_t)> distribute = [&](size_t i, size_t remaining) {
				if(i == r.N-1) {
					if(remaining < 1) return;
					sizes[i] = remaining;
					std::function<void(size_t, Node&)> product = [&](size_t j, Node& partial) {
						if(j == r.N) { out.push_back(partial); return; }
						for(const Node& c : all_trees(r.type(j), sizes[j])) {
							Node p2 = partial;
							p2.set_child(j, Node(c));
							product(j+1, p2);
						}
					};
					Node root = grammar.makeNode(&r);
					product(0, root);
					return;
				}
				for(size_t k = 1; k + (r.N-1-i) <= remaining; k++) {
					sizes[i] = k;
					distribute(i+1, remaining-k);
				}
			};
			distribute(0, s-1);
		}
	}
	return memo[key] = out;
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	Fleet fleet("GeneralEnumeration tests");
	fleet.initialize(argc, argv);

	const Enum E(&grammar);
	const nonterminal_t INT = grammar.nt<int>(), BOOL = grammar.nt<bool>(), CHAR = grammar.nt<char>(),
	                    DBL = grammar.nt<double>(), LONG = grammar.nt<long>(), FLT = grammar.nt<float>();

	// ---------------------------------------------------------------------------------------
	print("# 1. analysis: finiteness and counts");
	CHECK(E.count(CHAR).has_value() and *E.count(CHAR) == 3, "char count");
	CHECK(E.count(BOOL).has_value() and *E.count(BOOL) == 2+9+3, "bool count");
	CHECK(not E.count(INT).has_value(),  "int should be infinite");
	CHECK(not E.count(DBL).has_value(),  "double should be infinite");
	CHECK(not E.count(LONG).has_value(), "long should be infinite");
	CHECK(E.count(FLT).has_value() and *E.count(FLT) == 0, "float should be unproductive");

	// ---------------------------------------------------------------------------------------
	print("# 2. finite nonterminals: exactly the brute-force set, and out-of-range throws");
	for(nonterminal_t nt : {CHAR, BOOL}) {
		std::set<std::string> brute;
		for(size_t s = 1; s <= 12; s++) for(const Node& t : all_trees(nt, s)) brute.insert(t.string());
		const idx_t C = *E.count(nt);
		CHECK(brute.size() == C, "count vs brute force", nt, brute.size(), C);
		std::set<std::string> decoded;
		for(idx_t z = 0; z < C; z++) {
			Node t = E.toNode(z, nt);
			decoded.insert(t.string());
			CHECK(E.toInteger(t) == z, "finite round trip", nt, z);
		}
		CHECK(decoded == brute, "decoded set != brute-force set", nt);
		bool threw = false;
		try { (void)E.toNode(C, nt); } catch(const EnumerationOutOfRangeException&) { threw = true; }
		CHECK(threw, "toNode(count) should throw", nt);
	}
	{
		bool threw = false;
		try { (void)E.toNode(0, FLT); } catch(const EnumerationOutOfRangeException&) { threw = true; }
		CHECK(threw, "unproductive nonterminal should throw");
	}

	// ---------------------------------------------------------------------------------------
	print("# 3. every brute-force tree (by size) round-trips; codes are distinct");
	const size_t MAXS = 7;
	for(nonterminal_t nt : {INT, BOOL, CHAR, DBL, LONG}) {
		std::set<idx_t> codes;
		size_t n = 0;
		for(size_t s = 1; s <= MAXS; s++) {
			for(const Node& t : all_trees(nt, s)) {
				n++;
				const idx_t z = E.toInteger(t);
				CHECK(codes.insert(z).second, "duplicate code", nt, z, t.string());
				CHECK(E.toNode(z, nt) == t, "toNode(toInteger(t)) != t", nt, t.string());
			}
		}
		print("   nt", nt, ":", n, "trees with <=", MAXS, "nodes checked");
	}

	// ---------------------------------------------------------------------------------------
	print("# 4. integer prefix: toInteger(toNode(z)) == z, distinct trees, unproductive rule never used");
	for(nonterminal_t nt : {INT, DBL, LONG}) {
		std::set<std::string> seen;
		const idx_t Z = 300000;
		for(idx_t z = 0; z < Z; z++) {
			Node t = E.toNode(z, nt);
			CHECK(t.is_complete(), "incomplete tree", nt, z);
			CHECK(E.toInteger(t) == z, "toInteger(toNode(z)) != z", nt, z, t.string());
			CHECK(seen.insert(t.string()).second, "duplicate tree", nt, z, t.string());
			CHECK(t.string().find("bad(") == std::string::npos, "unproductive rule appeared", t.string());
		}
		print("   nt", nt, ": z <", Z, "ok; e.g. z=0 ->", E.toNode(0, nt).string(), " z=1 ->", E.toNode(1, nt).string());
	}

	// ---------------------------------------------------------------------------------------
	print("# 5. large random trees: a code either round-trips exactly or throws (never silently wrong)");
	{
		size_t ok = 0, overflow = 0;
		for(size_t i = 0; i < 20000; i++) {
			Node t = grammar.generate(INT);
			if(t.string().find("bad(") != std::string::npos) continue; // generate() can't produce these anyway
			try {
				const idx_t z = E.toInteger(t);
				CHECK(E.toNode(z, INT) == t, "large tree round trip", t.string());
				ok++;
			} catch(const EnumerationOverflowException&) { overflow++; }
		}
		print("   round-tripped:", ok, " overflow (thrown):", overflow);
		CHECK(ok > 0 and overflow > 0, "expected both cases to occur");
	}

	// ---------------------------------------------------------------------------------------
	print("# 6. pairing primitives vs exact 128-bit arithmetic");
	{
		__extension__ typedef unsigned __int128 u128;
		const u128 MAX = std::numeric_limits<idx_t>::max();
		std::mt19937_64 rng(12345);
		auto rs128 = [](u128 x, u128 y) { u128 m = std::max(x,y); return m*(m+1) + x - y; };

		// isqrt and decode exactly at/near perfect squares across the whole 64-bit range, and at the top
		for(idx_t m = 1; m != 0 and m <= (idx_t(1)<<32) - 1; m = (m < 1000 ? m+1 : m + m/97 + 1)) {
			for(idx_t z : {m*m - 1, m*m, m*m + 1, m*m + 2*m}) {
				const idx_t s = Enum::isqrt(z);
				CHECK((u128)s*s <= z and (u128)(s+1)*(s+1) > z, "isqrt", z);
				const auto [x, y] = Enum::rs_decode(z);
				CHECK(rs128(x, y) == z, "rs_decode", z);
			}
		}
		for(const idx_t z : {std::numeric_limits<idx_t>::max(), std::numeric_limits<idx_t>::max()-1, std::numeric_limits<idx_t>::max() - (idx_t(1)<<33)}) {
			const auto [x, y] = Enum::rs_decode(z);
			CHECK(rs128(x, y) == z, "rs_decode at top", z);
		}
		// encode: exact when it fits, throws when it doesn't
		for(size_t i = 0; i < 2000000; i++) {
			const idx_t x = rng() >> (rng() % 64), y = rng() >> (rng() % 64);
			const u128 exact = rs128(x, y);
			bool threw = false; idx_t got = 0;
			try { got = Enum::rs_encode(x, y); } catch(const EnumerationOverflowException&) { threw = true; }
			if(exact > MAX) CHECK(threw, "rs_encode should have thrown", x, y);
			else            CHECK(not threw and got == exact, "rs_encode wrong", x, y);
		}
	}

	// ---------------------------------------------------------------------------------------
	print("# 7. modifying the grammar after construction is detected");
	{
		bool threw = false;
		grammar.add("extra(%s)", +[](int a) -> int { return a; }, 0.1);
		try { (void)E.toNode(5, INT); } catch(const std::logic_error&) { threw = true; }
		CHECK(threw, "grammar change not detected");
	}

	if(nfail == 0) print("ALL TESTS PASSED");
	else           print(nfail, "FAILURES");
	return nfail == 0 ? 0 : 1;
}
