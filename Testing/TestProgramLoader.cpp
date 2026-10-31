// TestProgramLoader: ProgramLoader::was_called has a defined value in every freshly constructed
// hypothesis and lexicon.
//
// It used to be declared `bool was_called;` with no initializer, so a new hypothesis carried
// whatever bytes were in its memory, and copying or moving it read an indeterminate bool
// (undefined behavior; UBSan: "load of value 16, which is not a valid value for type 'bool'"
// on a moved Lexicon). To make that deterministic, each object here is constructed into memory
// pre-filled with 0x10 bytes, and the flag's byte is inspected with memcpy (which is not a bool
// load, so the test itself has no undefined behavior): it must be 0 after construction.

#include <cstring>
#include <new>

#include "TestModels.h"
#include "Lexicon.h"

using namespace TestHarness;

using H = FiniteHypothesis;

class Lex final : public Lexicon<Lex, int, H, short, int> {
public:
	using Super = Lexicon<Lex, int, H, short, int>;
	using Super::Super;
};

// construct a T into garbage-filled memory with make(buffer), return the raw byte of was_called
template<typename T, typename F>
unsigned char fresh_flag_byte(F make) {
	alignas(T) unsigned char buf[sizeof(T)];
	std::memset(buf, 0x10, sizeof(buf));
	T* p = make(static_cast<void*>(buf));
	unsigned char raw;
	std::memcpy(&raw, &p->was_called, 1);
	p->~T();
	return raw;
}

int main() {
	test_name = "ProgramLoader";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());

	Node prog = finite_grammar.simple_parse("plus(1,2)");   // LOTHypothesis(Node&) takes a non-const reference

	// ==================================================================================
	section("was_called is false in freshly constructed hypotheses (not leftover memory)");
	{
		CHECK(fresh_flag_byte<H>([](void* b) { return new (b) H(); }) == 0,
		      "default-constructed LOTHypothesis");
		CHECK(fresh_flag_byte<H>([&](void* b) { return new (b) H(prog); }) == 0,
		      "LOTHypothesis constructed from a Node");
		const H src(prog);
		CHECK(fresh_flag_byte<H>([&](void* b) { return new (b) H(src); }) == 0,
		      "copy of a fresh LOTHypothesis");
		CHECK(fresh_flag_byte<Lex>([](void* b) { return new (b) Lex(); }) == 0,
		      "default-constructed Lexicon");
		Lex l;
		l.factors[0] = H(prog);
		l.factors[1] = H(prog);
		CHECK(fresh_flag_byte<Lex>([&](void* b) { return new (b) Lex(std::move(l)); }) == 0,
		      "Lexicon move-constructed from a fresh one");
	}

	// ==================================================================================
	section("was_called still records calls; a copy starts fresh");
	{
		H h(prog);
		CHECK(not h.was_called, "not called yet");
		const int out = h.call(0, -999);
		CHECK(out == 3, "plus(1,2) evaluates to 3, got " << out);
		CHECK(h.was_called, "call() sets was_called");
		// LOTHypothesis's copy constructor goes through operator=, which does not copy
		// was_called, so a copy has its own (now defined) value: false. Before the
		// initializer, every copy -- MCMC makes many -- held an indeterminate bool here.
		H c(h);
		CHECK(not c.was_called, "a copy of a called hypothesis starts with was_called == false");
	}

	return finish();
}
