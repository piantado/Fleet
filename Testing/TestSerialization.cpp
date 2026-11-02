// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TestSerialization.cpp -- serialize/deserialize of Nodes, LOTHypotheses, Lexicons, TopNs,
// ConstantContainers, and save()/load() files. Every bug fixed in the serialization review
// has a regression check here:
//   * get_rule(nt, s) could not find an exact match that sorted after two longer formats it is
//     a prefix of (e.g. "x" after "xa","xb"), so deserialize failed on what serialize wrote
//   * no warning for rule formats that break serialization (';', newline, '|', NullRule format,
//     duplicate formats); tabs in formats broke LOTHypothesis::deserialize
//   * malformed input asserted/aborted (or was silently accepted: trailing junk in numbers,
//     stoul("-4") wrapping, leftover nodes) instead of throwing DeserializationError
//   * subnormal scores serialized fine but could not be read back (std::stod)
//   * partial hypotheses (null gaps) serialized fine but deserialize asserted while compiling
//   * TopN::deserialize printed every element when top_print_best was set
//   * save()/load() of a TopN (multi-line serialization) silently produced garbage; load of a
//     missing file returned an empty vector; blank/comment/CRLF lines broke load
//   * ConstantContainer constants were not serialized
//   * overloaded rules (same format and nonterminal, different argument types, e.g. ANSeq in
//     Number-Complex) could not be deserialized at all
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unistd.h>

#include "Fleet.h"
#include "Grammar.h"
#include "Builtins.h"
#include "DeterministicLOTHypothesis.h"
#include "Lexicon.h"
#include "TopN.h"
#include "IO.h"
#include "ConstantContainer.h"

#include "TestHarness.h"

using namespace TestHarness;

// Nonterminals: int (0), bool (1). Weights chosen so that "x" sorts AFTER "xa" and "xb" (rules are
// sorted by decreasing probability), which is the order that used to break exact-match lookup.
class SGrammar : public Grammar<int,int, int,bool> {
public:
	SGrammar() {
		add("xa",             +[]() -> int { return 10; }, 2.0);
		add("xb",             +[]() -> int { return 11; }, 2.0);
		add("x",              Builtins::X<SGrammar>, 1.0);
		add("inc(%s)",        +[](int a) -> int { return a+1; }, 1.0);
		add("if(%s,%s,%s)",   +[](bool c, int a, int b) -> int { return c ? a : b; }, 0.5);
		add("t\tt",           +[]() -> int { return 5; }, 0.5);   // a tab: fine now
		add("a:b",            +[]() -> int { return 6; }, 0.5);   // a ':' : fine (nt is before the FIRST ':')
		add("p|q",            +[]() -> int { return 7; }, 0.3);   // fine for LOTHypothesis, breaks a Lexicon (warns)
		add("semi;colon(%s)", +[](int a) -> int { return a-1; }, 0.1); // breaks deserialization (warns)
		add("n\nn",           +[]() -> int { return 8; }, 0.1);   // breaks TopN/save/load (warns)
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a < b; }, 1.0);
		add("ov(%s)",         +[](int a)  -> int { return a*2; }, 0.2);   // overloads: same format and nt,
		add("ov(%s)",         +[](bool b) -> int { return b ? 100 : 200; }, 0.2); // different argument types
	}
} sgrammar;

class SH : public DeterministicLOTHypothesis<SH,int,int,SGrammar,&sgrammar> {
public:
	using Super = DeterministicLOTHypothesis<SH,int,int,SGrammar,&sgrammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t& d) override { return call(d.input, -999) == d.output ? 0.0 : -1.0; }
};

class SL : public Lexicon<SL, std::string, SH, int, int> {
public:
	using Super = Lexicon<SL, std::string, SH, int, int>;
	using Super::Super;
};

// A hypothesis with constants, written the same way as Models/SymbolicRegression/MyHypothesis.h
class SC : public ConstantContainer, public DeterministicLOTHypothesis<SC,int,int,SGrammar,&sgrammar> {
public:
	using Super = DeterministicLOTHypothesis<SC,int,int,SGrammar,&sgrammar>;
	using Super::Super;
	double compute_single_likelihood(const datum_t&) override { return 0.0; }
	size_t count_constants() const override { return constants.size(); }
	std::pair<double,double> constant_proposal(Constant) const override { return {0.0, 0.0}; }
	void randomize_constants() override { }
	bool operator==(const SC& h) const override { return Super::operator==(h) and ConstantContainer::operator==(h); }
	size_t hash() const override { size_t h = Super::hash(); hash_combine(h, ConstantContainer::hash()); return h; }
	std::string serialize() const override { return serialize_constants() + Serialization::HypothesisDelimiter + Super::serialize(); }
	static SC deserialize(const std::string& s) {
		const auto f = split_first(s, Serialization::HypothesisDelimiter, 1, "SC");
		SC h = Super::deserialize(f[1]);
		h.deserialize_constants(f[0]);
		return h;
	}
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// helpers
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

bool same_scores(const SH& a, const SH& b) {
	return same_bits(a.prior, b.prior) and same_bits(a.likelihood, b.likelihood) and same_bits(a.posterior, b.posterior);
}

SH semicolon_hypothesis() { // can't come from from_parseable, since ';' splits the string
	Node v = sgrammar.makeNode(sgrammar.get_rule((nonterminal_t)0, std::string("semi;colon(%s)")));
	v.set_child(0, sgrammar.makeNode(sgrammar.get_rule((nonterminal_t)0, std::string("x"))));
	SH h(v); h.prior = h.likelihood = h.posterior = -1;
	return h;
}

std::vector<SH::datum_t> data1 = { SH::datum_t(1,2), SH::datum_t(3,10) };

SH make(const std::string& parseable, double pr=-1.0, double li=-2.0, double po=-3.0) {
	SH h(sgrammar.from_parseable(parseable));
	h.prior = pr; h.likelihood = li; h.posterior = po;
	return h;
}

template<typename F>
bool throws_deserialization(F f) {
	try { f(); } catch(DeserializationError&) { return true; } catch(...) { return false; }
	return false;
}
template<typename F>
std::string error_type(F f) {
	try { f(); } catch(DeserializationError&) { return "DeserializationError"; }
	catch(YouShouldNotBeHereError&) { return "YouShouldNotBeHereError"; }
	catch(std::exception& e) { return std::string("std::exception: ") + e.what(); }
	catch(...) { return "other"; }
	return "none";
}

// capture std::cerr while running f
template<typename F>
std::string capture_cerr(F f) {
	std::stringstream ss;
	auto old = std::cerr.rdbuf(ss.rdbuf());
	try { f(); } catch(...) { std::cerr.rdbuf(old); throw; }
	std::cerr.rdbuf(old);
	return ss.str();
}

// capture std::cout while running f
template<typename F>
std::string capture_cout(F f) {
	std::stringstream ss;
	auto old = std::cout.rdbuf(ss.rdbuf());
	try { f(); } catch(...) { std::cout.rdbuf(old); throw; }
	std::cout.rdbuf(old);
	return ss.str();
}

bool uses_bad_rule(const Node& n) {
	for(const auto& x : n) {
		if(contains(x.rule->format, ';') or contains(x.rule->format, '\n')) return true;
	}
	return false;
}

std::string tmpfile(const std::string& name) {
	return "/tmp/TestSerialization." + std::to_string(getpid()) + "." + name;
}

std::string slurp(const std::string& f) {
	std::ifstream fs(f); std::stringstream ss; ss << fs.rdbuf(); return ss.str();
}

int main() {
	test_name = "Serialization";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	const double INF = std::numeric_limits<double>::infinity();
	const double NAN_ = std::numeric_limits<double>::quiet_NaN();
	const double SUB  = std::numeric_limits<double>::denorm_min();

	// ==================================================================================
	section("rule lookup: exact match wins regardless of sort order");
	{
		// precondition: "x" really is sorted after both "xa" and "xb"
		int ix=-1, ia=-1, ib=-1, i=0;
		for(auto& r : sgrammar.rules[0]) { if(r.format=="x") ix=i; if(r.format=="xa") ia=i; if(r.format=="xb") ib=i; i++; }
		CHECK(ix > ia and ix > ib, "test precondition: x sorts after xa and xb (got " << ix << " " << ia << " " << ib << ")");

		CHECK(error_type([]{ (void)sgrammar.get_rule((nonterminal_t)0, std::string("x")); }) == "none", "get_rule(0, \"x\") finds the exact match");
		CHECK(sgrammar.get_rule((nonterminal_t)0, std::string("x"))->format == "x", "get_rule(0, \"x\") returns rule x");
		CHECK(sgrammar.get_rule((nonterminal_t)0, std::string("xa"))->format == "xa", "exact match xa");
		CHECK(sgrammar.get_rule((nonterminal_t)0, std::string("inc"))->format == "inc(%s)", "unique prefix still works");
		CHECK(error_type([]{ (void)sgrammar.get_rule((nonterminal_t)0, std::string("i")); }) == "YouShouldNotBeHereError", "ambiguous prefix throws");
		CHECK(error_type([]{ (void)sgrammar.get_rule((nonterminal_t)0, std::string("zzz")); }) == "YouShouldNotBeHereError", "no match throws");
		CHECK(error_type([]{ (void)sgrammar.get_rule((nonterminal_t)7, std::string("x")); }) == "YouShouldNotBeHereError", "out-of-range nonterminal throws");

		SH h = make("0:x");
		const std::string s = h.serialize();
		CHECK(error_type([&]{ (void)SH::deserialize(s); }) == "none", "deserialize of a hypothesis that is the terminal x: " << error_type([&]{ (void)SH::deserialize(s); }));
	}

	// ==================================================================================
	section("warnings for formats that break serialization");
	{
		const std::string w = capture_cerr([]{ SGrammar g; });
		CHECK(contains(w, "\"semi;colon(%s)\""), "warns about ';'");
		CHECK(contains(w, "\"n\nn\""),            "warns about a newline");
		CHECK(contains(w, "\"p|q\""),             "warns about '|'");
		CHECK(not contains(w, "\"t\tt\""),        "no warning for a tab");
		CHECK(not contains(w, "\"a:b\""),         "no warning for ':'");
		CHECK(not contains(w, "\"x\""),           "no warning for ordinary formats");
		CHECK(not contains(w, "\"ov(%s)\""),      "no warning for overloads that differ in argument types");

		struct G2 : public Grammar<int,int, int> {
			G2() {
				add("■", +[]() -> int { return 1; });
				add("dup",    +[]() -> int { return 2; });
				add("dup",    +[]() -> int { return 3; });
			}
		};
		const std::string w2 = capture_cerr([]{ G2 g; });
		CHECK(contains(w2, "NullRule"), "warns about the NullRule's format");
		CHECK(contains(w2, "another rule of the same nonterminal"), "warns about duplicate formats");
	}

	// ==================================================================================
	section("LOTHypothesis round trip (programs and exact scores)");
	{
		const std::vector<std::string> programs = {
			"0:x", "0:xa", "0:xb", "0:t\tt", "0:a:b", "0:p|q", "0:inc(%s);0:x",
			"0:if(%s,%s,%s);1:lt(%s,%s);0:x;0:t\tt;0:inc(%s);0:a:b;0:xb",
			"0:ov(%s);0:x", "0:ov(%s);1:T", "0:ov(%s);0:ov(%s);1:lt(%s,%s);0:ov(%s);0:x;0:xa",
		};
		const std::vector<double> scores = { 0.0, -0.0, -1.0, 0.1, -1e300, 1e-300, SUB, -SUB, -INF, INF, NAN_, -NAN_,
		                                     std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
		                                     std::numeric_limits<double>::min(), -123.456789012345678 };
		size_t bad = 0, n = 0;
		for(auto& p : programs) {
			for(size_t i=0;i<scores.size();i++) {
				SH h = make(p, scores[i], scores[(i+5)%scores.size()], scores[(i+11)%scores.size()]);
				const std::string s = h.serialize();
				n++;
				try {
					SH g = SH::deserialize(s);
					if(not (g == h) or not same_scores(g,h) or g.serialize() != s or g.call(3, -999) != h.call(3, -999)) {
						bad++; print("# mismatch", QQ(s), QQ(g.serialize()));
					}
				} catch(std::exception& e) { bad++; print("# threw", QQ(s), e.what()); }
			}
		}
		CHECK(bad == 0, bad << " of " << n << " hand-picked round trips failed (incl. tab/':'/'|' formats, subnormal, inf, nan, -0.0)");

		// random programs from the prior
		size_t rbad = 0, rn = 0;
		for(int i=0;i<20000;i++) {
			Node v = sgrammar.generate();
			if(uses_bad_rule(v)) continue;
			SH h(v);
			h.compute_posterior(data1);
			const std::string s = h.serialize();
			rn++;
			try {
				SH g = SH::deserialize(s);
				if(not (g == h) or not same_scores(g,h) or g.serialize() != s) rbad++;
			} catch(...) { rbad++; }
		}
		CHECK(rn > 10000 and rbad == 0, rbad << " of " << rn << " random prior round trips failed");
	}

	// ==================================================================================
	section("overloaded formats are told apart by argument types");
	{
		SH a = make("0:ov(%s);0:x"), b = make("0:ov(%s);1:T");
		CHECK(a.get_value().rule != b.get_value().rule, "test precondition: the two ov rules are different rules");
		CHECK(a.call(3, -999) == 6 and b.call(3, -999) == 100, "each overload is the one whose argument type matches");
		CHECK(SH::deserialize(a.serialize()).get_value().rule == a.get_value().rule and 
		      SH::deserialize(b.serialize()).get_value().rule == b.get_value().rule, "deserialize picks the right overload");
		// a gap as the only distinguishing child is ambiguous
		CHECK(throws_deserialization([]{ (void)sgrammar.from_parseable(std::string("0:ov(%s);0:\u25A0")); }), "overload with only a null child is ambiguous (throws)");
		CHECK(throws_deserialization([]{ (void)sgrammar.from_parseable(std::string("0:ov(%s);0:inc(%s)")); }), "overload whose child is incomplete throws");
	}
	
	// ==================================================================================
	section("partial hypotheses (null gaps) round trip");
	{
		Node v = sgrammar.makeNode(sgrammar.get_rule((nonterminal_t)0, std::string("if(%s,%s,%s)")));
		v.set_child(1, sgrammar.makeNode(sgrammar.get_rule((nonterminal_t)0, std::string("x"))));
		SH h; h.set_value(v, false); h.prior = -1; h.likelihood = 0; h.posterior = -1;
		const std::string s = h.serialize();
		std::string err = error_type([&]{ (void)SH::deserialize(s); });
		CHECK(err == "none", "deserialize of a partial tree " << QQ(s) << ": " << err);
		if(err == "none") {
			SH g = SH::deserialize(s);
			CHECK(g == h and not g.is_evaluable() and g.serialize() == s, "partial tree round trips");
		}
		SH e; e.prior = e.likelihood = e.posterior = 0;
		err = error_type([&]{ (void)SH::deserialize(e.serialize()); });
		CHECK(err == "none" and SH::deserialize(e.serialize()) == e, "an empty (null) hypothesis round trips: " << err);
	}

	// ==================================================================================
	section("malformed input throws DeserializationError (never asserts, never silently accepted)");
	{
		const std::vector<std::pair<std::string,std::string>> bad = {
			{"", "empty string"},
			{"-1\t-2\t-3", "too few fields"},
			{"-1\t-2\t-3\t", "empty tree"},
			{"-1.5xyz\t-2\t-3\t0:x", "trailing junk in a score"},
			{" -1\t-2\t-3\t0:x", "leading space in a score"},
			{"+1\t-2\t-3\t0:x", "leading + in a score"},
			{"-1\t\t-3\t0:x", "empty score"},
			{"-1\t-2\t-3\t0:inc(%s)", "missing child"},
			{"-1\t-2\t-3\t0:x;0:x", "leftover node"},
			{"-1\t-2\t-3\t9:x", "nonterminal out of range"},
			{"-1\t-2\t-3\t-1:x", "negative nonterminal"},
			{"-1\t-2\t-3\tq:x", "non-numeric nonterminal"},
			{"-1\t-2\t-3\tx", "no ':'"},
			{"-1\t-2\t-3\t0:zzz", "unknown rule"},
			{"-1\t-2\t-3\t0:if(%s,%s,%s);0:x;0:x;0:x", "type mismatch (int where bool is needed)"},
			{semicolon_hypothesis().serialize(), "a format containing ';'"},
		};
		for(auto& [s, what] : bad) {
			const auto e = error_type([&]{ (void)SH::deserialize(s); });
			CHECK(e == "DeserializationError", what << ": " << QQ(s) << " gave " << e);
		}
		CHECK(throws_deserialization([]{ (void)sgrammar.from_parseable(std::string("0:x;0:x")); }), "from_parseable rejects leftover nodes");
		CHECK(throws_deserialization([]{ (void)parse_exact<size_t>("-4"); }), "parse_exact<size_t> rejects a minus sign (stoul wraps)");
		CHECK(parse_exact<double>(str(SUB)) == SUB, "parse_exact reads subnormals (stod throws on them)");
	}

	// ==================================================================================
	section("Lexicon round trip and errors");
	{
		SL L;
		L.factors.emplace("a", make("0:inc(%s);0:x", -1, -2, -3));
		L.factors.emplace("b c", make("0:t\tt", -4, -5, -6));
		L.factors.emplace("", make("0:a:b", -7, -8, -9)); // empty key
		L.prior = -1.25; L.likelihood = SUB; L.posterior = -INF;
		const std::string s = L.serialize();
		const auto e = error_type([&]{ (void)SL::deserialize(s); });
		CHECK(e == "none", "Lexicon deserialize: " << e);
		if(e == "none") {
			SL M = SL::deserialize(s);
			bool ok = M == L and M.serialize() == s and same_bits(M.prior, L.prior) and same_bits(M.likelihood, L.likelihood) and same_bits(M.posterior, L.posterior);
			for(auto& [k,f] : L.factors) ok = ok and M.factors.contains(k) and same_scores(M.factors.at(k), f);
			CHECK(ok, "Lexicon round trip is exact (keys, factors, all scores)");
		}

		// '|' in a key or factor: a warning at serialize, an error at deserialize
		SL P; P.factors.emplace("k|ey", make("0:x")); P.prior = P.likelihood = P.posterior = 0;
		std::string ps;
		const std::string w = capture_cerr([&]{ ps = P.serialize(); });
		CHECK(contains(w, "cannot be deserialized"), "Lexicon warns when a key contains '|'");
		CHECK(throws_deserialization([&]{ (void)SL::deserialize(ps); }), "...and deserialize throws");

		CHECK(throws_deserialization([]{ (void)SL::deserialize("0|0"); }), "Lexicon: too few fields");
		CHECK(throws_deserialization([]{ (void)SL::deserialize("0|0|0|a"); }), "Lexicon: key without a factor");
		CHECK(throws_deserialization([&]{
			(void)SL::deserialize("0|0|0|a|" + make("0:x").serialize() + "|a|" + make("0:xa").serialize()); }), "Lexicon: duplicate key");
	}

	// ==================================================================================
	section("TopN round trip, printing, and errors");
	{
		TopN<SH> t(10);
		for(auto p : {"0:x", "0:xa", "0:xb", "0:inc(%s);0:x", "0:t\tt", "0:p|q", "0:a:b"}) {
			SH h = make(p); h.compute_posterior(data1);
			t << h;
		}
		CHECK(t.size() == 7, "test precondition: 7 distinct elements");
		const std::string s = t.serialize();

		const bool old = FleetArgs::top_print_best;
		FleetArgs::top_print_best = true;
		TopN<SH> d(1);
		const std::string printed = capture_cout([&]{ d = TopN<SH>::deserialize(s); });
		FleetArgs::top_print_best = old;

		CHECK(printed.empty(), "TopN::deserialize prints nothing even when top_print_best is set (printed " << printed.size() << " chars)");
		bool same = d.N == t.N and d.size() == t.size() and d.serialize() == s;
		for(auto& h : t.values()) same = same and d.contains(h);
		CHECK(same, "TopN round trip (N, elements, exact scores)");

		CHECK(TopN<SH>::deserialize("5").size() == 0 and TopN<SH>::deserialize("5").N == 5, "empty TopN round trips");
		CHECK(TopN<SH>::deserialize(TopN<SH>(0).serialize()).N == 0, "N=0 round trips");
		CHECK(TopN<SH>::deserialize(TopN<SH>(TopN<SH>::MAX_N).serialize()).N == TopN<SH>::MAX_N, "N=MAX_N round trips");

		CHECK(throws_deserialization([]{ (void)TopN<SH>::deserialize(""); }), "TopN: empty string");
		CHECK(throws_deserialization([]{ (void)TopN<SH>::deserialize("-4"); }), "TopN: negative N");
		CHECK(throws_deserialization([]{ (void)TopN<SH>::deserialize("x"); }), "TopN: non-numeric N");
		CHECK(throws_deserialization([&]{ (void)TopN<SH>::deserialize("1\n" + make("0:x").serialize() + "\n" + make("0:xa").serialize()); }), "TopN: more elements than N");
		CHECK(throws_deserialization([&]{ (void)TopN<SH>::deserialize("3\n" + make("0:x").serialize() + "\n" + make("0:x").serialize()); }), "TopN: duplicate element");
		CHECK(throws_deserialization([&]{ (void)TopN<SH>::deserialize("3\n" + make("0:x",0,0,-INF).serialize()); }), "TopN: -inf posterior element");

		// serialize while other threads add (run under SANITIZE=1/TSan to see the old race)
		TopN<SH> c(50);
		std::atomic<bool> stop{false};
		std::thread adder([&]{ for(int i=0;i<20000 and not stop;i++) { SH h(sgrammar.generate()); if(uses_bad_rule(h.get_value())) continue; h.prior=h.posterior=-(double)(i%997); h.likelihood=0; c << h; } });
		size_t okn = 0;
		for(int i=0;i<200;i++) { auto x = TopN<SH>::deserialize(c.serialize()); okn += (x.N == 50); }
		stop = true; adder.join();
		CHECK(okn == 200, "serialize concurrently with adds gives parseable output");
	}

	// ==================================================================================
	section("save()/load() files");
	{
		std::vector<SH> v;
		for(auto p : {"0:x", "0:xa", "0:inc(%s);0:x", "0:t\tt", "0:p|q", "0:a:b"}) v.push_back(make(p, -1, SUB, -INF));
		const auto f = tmpfile("v.txt");
		save(f, v);
		auto w = load<SH>(f);
		bool same = w.size() == v.size();
		for(size_t i=0;same and i<v.size();i++) same = w[i] == v[i] and same_scores(w[i], v[i]);
		CHECK(same, "vector round trip through save/load");

		// blank lines, comments, and CRLF are skipped
		{ std::ofstream o(f); o << "# a comment\n\n" << v[0].serialize() << "\r\n\n" << v[1].serialize() << "\n# end\n"; }
		w = load<SH>(f);
		CHECK(w.size() == 2 and w[0] == v[0] and w[1] == v[1], "load skips blank lines, # comments, and \\r");

		// a bad line reports the file and line
		{ std::ofstream o(f); o << v[0].serialize() << "\n" << "garbage\n"; }
		std::string msg;
		try { (void)load<SH>(f); } catch(DeserializationError& e) { msg = e.what(); }
		CHECK(contains(msg, f) and contains(msg, "line 2"), "load error names the file and line: " << QQ(msg));

		CHECK(error_type([]{ (void)load<SH>("/nonexistent/dir/file.txt"); }) != "none", "load of a missing file throws (used to return an empty vector)");
		CHECK(error_type([&]{ save("/nonexistent/dir/file.txt", v); }) != "none", "save to an unwritable path throws");

		// the generic line-based save refuses multi-line serializations (it used to write them, and
		// load read back one empty TopN per line)
		TopN<SH> t(5); for(auto& h : v) { SH g = h; g.posterior = g.prior = -1.0 * (double)(&h - &v[0]); t << g; }
		std::vector<TopN<SH>> vt = { t };
		const auto ft = tmpfile("vt.txt");
		CHECK(error_type([&]{ save(ft, vt); }) != "none", "save of a vector of TopNs throws instead of writing an unreadable file");

		// a newline in a rule format: also refused by save
		std::vector<SH> vn = { make("0:n\nn") };
		CHECK(error_type([&]{ save(ft, vn); }) != "none", "save of a hypothesis whose format contains a newline throws");

		// TopN-specific save/load
		save(ft, t);
		TopN<SH> u = load_topn<SH>(ft);
		CHECK(u.N == t.N and u.serialize() == t.serialize(), "save(TopN)/load_topn round trip");
		const TopN<SH>& ct = t;
		save(ft, ct);
		CHECK(load_topn<SH>(ft).serialize() == t.serialize(), "save(const TopN&) picks the TopN overload");
		CHECK(error_type([]{ (void)load_topn<SH>("/nonexistent/dir/file.txt"); }) != "none", "load_topn of a missing file throws");

		std::remove(f.c_str()); std::remove(ft.c_str());
	}

	// ==================================================================================
	section("ConstantContainer constants are serialized");
	{
		const std::vector<std::vector<float>> cs = { {}, {1.0f}, {0.1f, -2.5e-7f, 3.4e38f, std::numeric_limits<float>::denorm_min(), -0.0f} };
		size_t bad = 0;
		for(auto& c : cs) {
			SC h(sgrammar.from_parseable(std::string("0:inc(%s);0:x")));
			for(auto x : c) h.constants.emplace_back(x);
			h.prior = -1; h.likelihood = -2; h.posterior = -3;
			const std::string s = h.serialize();
			try {
				SC g = SC::deserialize(s);
				bool ok = g == h and g.constants.size() == h.constants.size() and g.serialize() == s;
				for(size_t i=0;ok and i<c.size();i++) { float a = g.constants[i].get_value(), b = h.constants[i].get_value(); ok = std::memcmp(&a,&b,sizeof(float)) == 0; }
				if(not ok) { bad++; print("# mismatch", QQ(s), QQ(g.serialize())); }
			} catch(std::exception& e) { bad++; print("# threw", QQ(s), e.what()); }
		}
		CHECK(bad == 0, bad << " ConstantContainer round trips failed");
		CHECK(throws_deserialization([]{ (void)SC::deserialize("1@x\t-1\t-2\t-3\t0:x"); }), "bad constant throws");
	}

	return finish();
}
