// TestCompilation: Node -> Program linearization and execution on the VirtualMachineState.
//
//  1. Differential testing: tens of thousands of random programs from a grammar with arithmetic,
//     comparisons, short-circuit and/or, if, not, implies, iff, and non-commutative operators are
//     compiled and run on many inputs, and compared with an independent tree-walking interpreter
//     written here (ref()).
//  2. Evaluation order: primitives that record when they run ("tr(...)") check that arguments are
//     evaluated left to right, exactly once, and that the branches skipped by if/and/or are NOT
//     evaluated at all (short-circuiting), by comparing traces with the interpreter's trace.
//  3. Program size: one instruction per node plus one jump per if.
//  4. Hand-written programs parsed from strings (simple_parse and from_parseable), with known
//     outputs; recursion (factorial, fibonacci), the recursion limit and runaway programs returning
//     the error value; and string()/parse round trips of random programs.
//  5. Hypothesis copies run correctly (the program's loader is the copy), and serialize/deserialize.

#include <map>
#include <string>
#include <vector>

#include "Fleet.h"
#include "Grammar.h"
#include "Builtins.h"
#include "DeterministicLOTHypothesis.h"

#include "TestHarness.h"

using namespace TestHarness;

// the trace of primitive calls: each traced primitive appends its argument
thread_local std::vector<long> TRACE;

class G : public Grammar<int,int, int,bool> {
public:
	G() {
		add("x",              Builtins::X<G>, 3.0);
		add("0",              +[]() -> int { return 0; }, 1.0);
		add("1",              +[]() -> int { return 1; }, 1.0);
		add("2",              +[]() -> int { return 2; }, 1.0);
		add("add(%s,%s)",     +[](int a, int b) -> int { return a+b; }, 1.0);
		add("sub(%s,%s)",     +[](int a, int b) -> int { return a-b; }, 1.0);    // non-commutative
		add("mul(%s,%s)",     +[](int a, int b) -> int { return a*b; }, 0.5);
		add("pow2(%s,%s)",    +[](int a, int b) -> int { return a*2 + b*3 - 1; }, 0.5); // non-commutative, distinguishes argument order
		add("sel3(%s,%s,%s)", +[](int a, int b, int c) -> int { return 100*a + 10*b + c; }, 0.3); // 3-arg order
		add("tr(%s)",         +[](int a) -> int { TRACE.push_back(a); return a; }, 0.6); // traced identity
		add("if(%s,%s,%s)",   Builtins::If<G,int>, 0.6);
		add("rec(%s)",        Builtins::Recurse<G>, 0.0);  // only used in hand-written programs
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a<b; }, 1.0);
		add("eq(%s,%s)",      +[](int a, int b) -> bool { return a==b; }, 1.0);
		add("and(%s,%s)",     Builtins::And<G>, 0.6);
		add("or(%s,%s)",      Builtins::Or<G>, 0.6);
		add("not(%s)",        Builtins::Not<G>, 0.5);
		add("implies(%s,%s)", Builtins::Implies<G>, 0.4);
		add("iff(%s,%s)",     Builtins::Iff<G>, 0.4);
		add("trb(%s)",        +[](bool a) -> bool { TRACE.push_back(a ? 1000001 : 1000000); return a; }, 0.6); // traced bool
		add("T",              +[]() -> bool { return true; }, 0.7);
		add("F",              +[]() -> bool { return false; }, 0.7);
	}
} grammar;

class Prog : public DeterministicLOTHypothesis<Prog, int, int, G, &grammar> {
public:
	using Super = DeterministicLOTHypothesis<Prog, int, int, G, &grammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override { return call(d.input, -12345) == d.output ? 0.0 : -1.0; }
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The independent interpreter. Integer arithmetic is done in the same int type (Fleet compiles
// with -fwrapv, and so does this file, so overflow wraps identically in both).
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

std::vector<long> REF_TRACE;

long ref(const Node& n, int x) {
	const std::string& f = n.rule->format;
	auto c = [&](size_t i) { return ref(n.child(i), x); };
	if(f == "x") return x;
	if(f == "0") return 0;
	if(f == "1") return 1;
	if(f == "2") return 2;
	if(f == "add(%s,%s)")     { int a = c(0); int b = c(1); return (int)(a+b); }
	if(f == "sub(%s,%s)")     { int a = c(0); int b = c(1); return (int)(a-b); }
	if(f == "mul(%s,%s)")     { int a = c(0); int b = c(1); return (int)(a*b); }
	if(f == "pow2(%s,%s)")    { int a = c(0); int b = c(1); return (int)(a*2 + b*3 - 1); }
	if(f == "sel3(%s,%s,%s)") { int a = c(0); int b = c(1); int d = c(2); return (int)(100*a + 10*b + d); }
	if(f == "tr(%s)")         { int a = c(0); REF_TRACE.push_back(a); return a; }
	if(f == "if(%s,%s,%s)")   { return c(0) ? c(1) : c(2); }               // only one branch is evaluated
	if(f == "lt(%s,%s)")      { int a = c(0); int b = c(1); return a < b; }
	if(f == "eq(%s,%s)")      { int a = c(0); int b = c(1); return a == b; }
	if(f == "and(%s,%s)")     { return c(0) ? (c(1) != 0) : false; }        // short circuit
	if(f == "or(%s,%s)")      { return c(0) ? true : (c(1) != 0); }         // short circuit
	if(f == "not(%s)")        { return not c(0); }
	if(f == "implies(%s,%s)") { bool a = c(0); bool b = c(1); return (not a) or b; }
	if(f == "iff(%s,%s)")     { bool a = c(0); bool b = c(1); return a == b; }
	if(f == "trb(%s)")        { bool a = c(0); REF_TRACE.push_back(a ? 1000001 : 1000000); return a; }
	if(f == "T") return 1;
	if(f == "F") return 0;
	throw std::runtime_error("ref: unknown " + f);
}

Node random_program(size_t maxn=60) {
	while(true) { Node t = grammar.generate(); if(t.count() <= maxn) return t; }
}

int main() {
	test_name = "Compilation";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	// ==================================================================================
	section("random programs: compiled VM output == interpreter output, and identical traces");
	{
		const int NPROG = 30000;
		const std::vector<int> inputs = {-7, -2, -1, 0, 1, 2, 3, 10, 1000};
		int bad_out = 0, bad_trace = 0, bad_size = 0, used_bool_ops = 0;
		std::map<std::string,int> rule_use;
		for(int r=0;r<NPROG;r++) {
			Node t = random_program();
			for(auto& n : t) rule_use[n.rule->format]++;
			Prog h(t);
			size_t nif = 0; for(auto& n : t) nif += n.rule->is_a(Op::If);
			if(h.program.size() != t.count() + nif) { if(bad_size++ < 3) CHECK(false, "program size " << h.program.size() << " for " << t.count() << " nodes and " << nif << " ifs: " << t); }
			for(int x : inputs) {
				TRACE.clear(); REF_TRACE.clear();
				const long want = ref(t, x);
				const int got = h.call(x, -12345);
				if(got != (int)want) { if(bad_out++ < 5) CHECK(false, t << " on x=" << x << ": VM " << got << " vs interpreter " << want); }
				if(TRACE != REF_TRACE) { if(bad_trace++ < 5) CHECK(false, t << " on x=" << x << ": VM trace " << str(TRACE) << " vs interpreter " << str(REF_TRACE)); }
			}
		}
		for(auto& r : grammar) if(r.p > 0) CHECK(rule_use[r.format] > 100, "random programs exercised rule " << r.format);
		CHECK(bad_out == 0, bad_out << " (program,input) pairs gave a different output from the interpreter");
		CHECK(bad_trace == 0, bad_trace << " (program,input) pairs evaluated primitives in a different order, a different number of times, or evaluated a short-circuited branch");
		CHECK(bad_size == 0, bad_size << " programs had an unexpected instruction count");
		(void)used_bool_ops;
	}

	// ==================================================================================
	section("hand-written programs with known outputs");
	{
		struct Case { std::string program; int x; int expect; std::vector<long> trace; };
		const std::vector<Case> cases = {
			{"sub(x,1)", 10, 9, {}},
			{"sub(1,x)", 10, -9, {}},
			{"pow2(x,1)", 5, 12, {}},
			{"pow2(1,x)", 5, 16, {}},
			{"sel3(1,2,x)", 7, 127, {}},
			{"sel3(x,0,2)", 7, 702, {}},
			{"sel3(tr(1),tr(2),tr(x))", 3, 123, {1,2,3}},
			{"add(tr(x),tr(sub(x,1)))", 5, 9, {5,4}},
			{"if(lt(x,2),tr(1),tr(2))", 0, 1, {1}},
			{"if(lt(x,2),tr(1),tr(2))", 9, 2, {2}},
			{"if(trb(T),tr(1),tr(2))", 0, 1, {1000001,1}},
			{"if(and(trb(F),trb(T)),1,2)", 0, 2, {1000000}},          // and short-circuits on false
			{"if(and(trb(T),trb(F)),1,2)", 0, 2, {1000001,1000000}},
			{"if(or(trb(T),trb(F)),1,2)", 0, 1, {1000001}},           // or short-circuits on true
			{"if(or(trb(F),trb(T)),1,2)", 0, 1, {1000000,1000001}},
			{"if(implies(T,F),1,2)", 0, 2, {}},
			{"if(implies(F,T),1,2)", 0, 1, {}},
			{"if(implies(F,F),1,2)", 0, 1, {}},
			{"if(implies(T,T),1,2)", 0, 1, {}},
			{"if(implies(lt(x,0),F),1,2)", 5, 1, {}},                  // F -> F is true
			{"if(iff(T,F),1,2)", 0, 2, {}},
			{"if(iff(F,F),1,2)", 0, 1, {}},
			{"if(not(eq(x,2)),1,2)", 2, 2, {}},
			{"if(lt(x,0),if(lt(x,sub(0,2)),tr(0),tr(1)),if(lt(x,2),tr(2),tr(x)))", -9, 0, {0}},
			{"if(lt(x,0),if(lt(x,sub(0,2)),tr(0),tr(1)),if(lt(x,2),tr(2),tr(x)))", -1, 1, {1}},
			{"if(lt(x,0),if(lt(x,sub(0,2)),tr(0),tr(1)),if(lt(x,2),tr(2),tr(x)))", 1, 2, {2}},
			{"if(lt(x,0),if(lt(x,sub(0,2)),tr(0),tr(1)),if(lt(x,2),tr(2),tr(x)))", 8, 8, {8}},
			// recursion: factorial and fibonacci
			{"if(lt(x,2),1,mul(x,rec(sub(x,1))))", 0, 1, {}},
			{"if(lt(x,2),1,mul(x,rec(sub(x,1))))", 1, 1, {}},
			{"if(lt(x,2),1,mul(x,rec(sub(x,1))))", 5, 120, {}},
			{"if(lt(x,2),1,mul(x,rec(sub(x,1))))", 10, 3628800, {}},
			{"if(lt(x,2),x,add(rec(sub(x,1)),rec(sub(x,2))))", 6, 8, {}},   // 24 recursive calls (MAX_RECURSE counts CALLS, not depth)
			{"if(lt(x,1),0,add(tr(x),rec(sub(x,1))))", 3, 6, {3,2,1}},
		};
		for(const auto& c : cases) {
			bool parsed = false;
			try {
				Node t = grammar.simple_parse(c.program);
				parsed = true;
				CHECK(t.string() == c.program, "string() of parsed " << c.program << " is " << t.string());
				Prog h(t);
				TRACE.clear();
				const int got = h.call(c.x, -12345);
				CHECK(got == c.expect, c.program << " on x=" << c.x << " gave " << got << ", expected " << c.expect);
				CHECK(TRACE == c.trace, c.program << " on x=" << c.x << " trace " << str(TRACE) << ", expected " << str(c.trace));
				// the same via from_parseable and via a copy of the hypothesis (whose loader must be the copy)
				Prog h2(grammar.from_parseable(t.parseable()));
				Prog h3 = h;
				TRACE.clear();
				CHECK(h2.call(c.x, -12345) == c.expect and h3.call(c.x, -12345) == c.expect, "from_parseable / copied hypothesis for " << c.program);
				CHECK(h3.program.loader == &h3, "copied hypothesis's program loader is the copy");
			} catch(std::exception& e) {
				CHECK(false, "could not parse/run " << c.program << (parsed ? " (run)" : " (parse)"));
			}
		}
	}

	// ==================================================================================
	section("recursion limits and runaway programs return the error value");
	{
		Prog inf(grammar.simple_parse("rec(x)"));
		CHECK(inf.call(3, -777) == -777, "infinite recursion returns the error value");
		Prog deep(grammar.simple_parse("if(lt(x,1),0,add(1,rec(sub(x,1))))"));
		CHECK(deep.call(30, -777) == 30, "recursion depth 30 is fine");
		// MAX_RECURSE is documented as "the max number of times we can call recurse"; deep(n) makes n calls
		const int mr = (int)VirtualMachineControl::MAX_RECURSE;
		CHECK(deep.call(mr - 1, -777) == mr - 1, "MAX_RECURSE-1 recursive calls work");
		CHECK(deep.call(mr, -777) == mr, "exactly MAX_RECURSE recursive calls work");
		CHECK(deep.call(mr + 1, -777) == -777, "MAX_RECURSE+1 recursive calls should fail, but return " << deep.call(mr + 1, -777)
		      << " (Recurse tests recursion_depth++ > MAX_RECURSE, which allows MAX_RECURSE+1 calls)");
		CHECK(deep.call(mr + 5, -777) == -777, "well over MAX_RECURSE returns the error value");
		Prog fib(grammar.simple_parse("if(lt(x,2),x,add(rec(sub(x,1)),rec(sub(x,2))))"));
		CHECK(fib.call(20, -777) == -777, "fib(20) needs ~22000 calls and returns the error value");
		CHECK(fib.call(6, -777) == 8, "and the VM state is fresh on the next call");
		// MAX_RUN_PROGRAM bounds the number of instructions
		const unsigned long old = VirtualMachineControl::MAX_RUN_PROGRAM;
		VirtualMachineControl::MAX_RUN_PROGRAM = 20;
		Prog big(grammar.simple_parse("add(add(add(add(x,1),add(x,1)),add(add(x,1),add(x,1))),add(add(add(x,1),add(x,1)),add(add(x,1),add(x,1))))"));
		const int big_out = big.call(1, -777);
		Prog small(grammar.simple_parse("add(add(x,1),add(x,1))"));
		const int small_out = small.call(1, -777);
		VirtualMachineControl::MAX_RUN_PROGRAM = old;
		CHECK(big_out == -777, "a 31-instruction program with MAX_RUN_PROGRAM=20 returns the error value (got " << big_out << ")");
		CHECK(small_out == 4, "a 7-instruction program runs under MAX_RUN_PROGRAM=20");
	}

	// ==================================================================================
	section("parsing round trips of random programs");
	{
		int bad = 0;
		for(int r=0;r<5000;r++) {
			Node t = random_program();
			try {
				Node a = grammar.simple_parse(t.string());
				Node b = grammar.from_parseable(t.parseable());
				if(not (a == t and b == t)) { if(bad++ < 3) CHECK(false, "parse round trip of " << t); }
			} catch(...) { if(bad++ < 3) CHECK(false, "parse threw on " << t); }
		}
		CHECK(bad == 0, bad << " random programs did not round trip through simple_parse/from_parseable");
		CHECK(grammar.simple_parse("  add(x,1) ") == grammar.simple_parse("add(x,1)"), "simple_parse strips surrounding whitespace");
		// simple_parse is documented to "remove the lambda x. if its there" -- which is what LOTHypothesis::string() prefixes
		bool lam = false;
		try { lam = grammar.simple_parse(LAMBDAXDOT_STRING + "add(x,1)") == grammar.simple_parse("add(x,1)"); } catch(...) {}
		CHECK(lam, "simple_parse does not strip a leading lambda-x-dot (it compares s.substr(0,3) to LAMBDAXDOT_STRING, but "
		      "the lambda is 2 bytes in UTF-8 so that string is 4 bytes long), so it cannot parse LOTHypothesis::string()");
	}

	// ==================================================================================
	section("hypothesis serialization");
	{
		std::vector<Prog::datum_t> data = { Prog::datum_t(1, 2), Prog::datum_t(2, 3) };
		for(int r=0;r<2000;r++) {
			Prog h(random_program());
			h.compute_posterior(data);
			const std::string s = h.serialize();
			Prog d = Prog::deserialize(s);
			const bool ok = d == h and d.prior == h.prior and d.posterior == h.posterior
			                and (d.likelihood == h.likelihood or (std::isnan(d.likelihood) and std::isnan(h.likelihood)));
			if(not ok) { CHECK(false, "serialize/deserialize round trip: " << s); break; }
			CHECK(d.call(5, -1) == h.call(5, -1), "deserialized hypothesis computes the same function");
		}
	}

	return finish();
}
