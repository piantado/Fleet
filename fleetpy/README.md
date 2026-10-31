# fleetpy

Declare Fleet models in Python; run them as compiled Fleet C++.

fleetpy is a **staged compiler**, not a runtime binding. Your Python declares types,
primitives, a likelihood and data. fleetpy writes the same C++ you would write by hand
(`Grammar<...>`, a `final` `DeterministicLOTHypothesis`), compiles it into a Python
extension (cached by content hash), and runs inference in C++ with the GIL released.
Nothing in the MCMC loop touches Python unless you opt in with a Python-callback primitive.

**Models are written in Python.** Primitive bodies, priors and likelihoods are ordinary
(annotated) Python that fleetpy compiles to C++ and checks against the Python source on
every build. Inline C++ (`cpp=...`) remains available as an escape hatch, for example to
call existing C++ code, but none of the examples use it.

```python
import fleetpy as fl
from fleetpy import builtins as B

@fl.struct
class Obj:
    shape: fl.i8
    color: fl.i8
    size: fl.i8

g = fl.Grammar(input=Obj, output=bool)

@g.primitive                               # prints as blue(%s)
def blue(x: Obj) -> bool:
    return x.color == 0

@g.primitive                               # compiled to C++, differential-tested vs. this source
def bigger(a: Obj, b: Obj) -> bool:
    return a.size > b.size

g.add_builtin(B.And, B.Or, B.Not, B.X)    # Fleet's own builtins

class H(fl.DeterministicHypothesis, grammar=g):
    likelihood = fl.likelihoods.noisy_exact()

model = fl.build(H)
data = [fl.Datum(Obj(1, 0, 0), True, 0.75), fl.Datum(Obj(2, 0, 0), False, 0.75)]
res = model.run(data, fl.ParallelTempering(chains=8, max_temp=10.0),
                control=fl.Control(time="30s", threads=8, seed=1), top=25)
for h in res.top:
    print(h.posterior, h)
model.parse("and(blue(x),bigger(x,x))").call(Obj(0, 0, 2))
```

## Quick start

```bash
pip install nanobind pytest          # nanobind builds the Python modules; pytest runs the tests
cd Fleet/fleetpy
python3 examples/rational_rules.py   # first build of a model takes ~10-20 s; later runs are cached
```

To use fleetpy from your own scripts, put this directory on the path
(`export PYTHONPATH=/path/to/Fleet/fleetpy`) and `import fleetpy as fl`. It finds Fleet
itself through the tree it lives in (or `FLEET_ROOT`).

## Examples

Each example is a Fleet model rewritten in **pure Python** (no C++ strings), and each is
tested to be **bit-identical to the C++ model**: same seed, same top hypotheses with
identical posteriors, and the same hash over every sample of the chain. Run any of them
directly (`python3 examples/number.py`).

| Example | Fleet model | What it exercises |
| --- | --- | --- |
| `rational_rules.py` | `Models/RationalRules` | structs, short-circuit builtins |
| `number.py` | `Models/Number-Simple` | `Recurse`; a Python prior with a recursion penalty; a Python whole-dataset likelihood; sets as bit masks (`&`, `\|`, `~`, `bit_count`) in a distinct `Set` type; two same-format `if` rules |
| `formal_language.py` | `Models/FormalLanguageTheory-Simple` | stochastic hypothesis, `flip()`, `Recurse` + `MemRecurse`, string slicing and `fl.char`, constant terminals, a custom proposal, the string-edit likelihood, VM limits |
| `lexicon_arith.py` | (hand-written Fleet lexicon in `tests/native/lex_driver.cpp`) | `Lexicon`, `LexiconRecurse`, `LexiconMemRecurse` |

The native references in `tests/native/` include Fleet's own `Main.cpp` files verbatim:
RationalRules through its `NO_INCLUDE_MAIN` guard, FormalLanguageTheory-Simple through
`DO_NOT_INCLUDE_MAIN` (with a stub for an `EigenLib.h` it doesn't use), and Number-Simple,
which has no guard, by renaming its `main` with the preprocessor.

## Hypothesis kinds

| Class | Generates | `call(x)` returns |
| --- | --- | --- |
| `fl.DeterministicHypothesis` | `DeterministicLOTHypothesis` | the output (raises `fl.Undefined` on error) |
| `fl.StochasticHypothesis` | `StochasticLOTHypothesis` | `{output: log mass}`, Fleet's unnormalized `DiscreteDistribution` |
| `fl.Lexicon` | `Lexicon<H, key, FPInner, in, out>` | as its factors (deterministic or `stochastic=True`) |

Stochastic hypotheses also have `distribution(x, normalize=True)` and `logprob(x, y)`.
Their output distribution is missing whatever mass was pruned or errored, which is governed
by `vm = fl.VMLimits(max_steps=, max_outputs=, min_lp=, max_recurse=, max_run_program=)`
(Fleet's `VirtualMachineControl`; defaults are Fleet's). These limits are part of the model
and part of the build hash.

```python
class Lex(fl.Lexicon, grammar=g, keys=[0, 1], main=1):    # stochastic=False, require_all_called=False
    likelihood = fl.likelihoods.noisy_exact()

h = model.parse({0: "add(x,x)", 1: "inc(F(0,x))"})        # a lexicon parses from {key: program}
h.factors                                                 # {0: "λx.add(x,x)", 1: "λx.inc(F(0,x))"}
```

A call runs the `main` factor; factors reach each other through
`B.LexiconRecurse(key=int)` (and the Safe/Mem variants). Keys are all ints (C++ `int`) or
all strs, matching Fleet's `ProgramLoader` overloads. If no rule produces the key type,
fleetpy adds one terminal per key (`0`, `1`, ... or `'a'`, `'b'`, ...). The key type may not
be used by any other rule. The prior is Fleet's Lexicon prior. Fleet's `Lexicon` copies leave
its factors' `program.loader` pointing at the source, so the generated code re-points it on
every call, as `Models/FormalLanguageTheory-Complex` does.

## Builtins

`fl.builtins` exposes Fleet's builtins with Fleet's usual formats:

* logic and control: `X`, `And`, `Or`, `Not`, `Implies`, `Iff`, `If(T)`
* stochastic (need a stochastic hypothesis): `Flip` (`flip()`), `FlipP`, `SafeFlipP`, `Sample(T)`
* recursion into the hypothesis: `Recurse`, `SafeRecurse`, `MemRecurse`, `SafeMemRecurse`
* recursion into a lexicon factor: `LexiconRecurse(key=)`, `LexiconSafeRecurse`, `LexiconMemRecurse`,
  `LexiconSafeMemRecurse`

Misuse is a build error, not a crash. That covers `Flip` in a deterministic hypothesis
(which would dereference a null VM pool), `Recurse` inside a Lexicon (Fleet throws
NotImplementedError), `Lexicon*Recurse` outside one, `Safe*` on an input type without
`size()`, and a likelihood written for the other kind.

Constants: `g.add_terminal("'0'", "0", fl.char, p=1.5)` (Fleet's `add_terminal`). A format
may be reused for rules of *different* return types (Fleet's Number model has two
`if(%s,%s,%s)`); the parser picks by type.

## Proposals

`proposal = fl.proposals.regenerate()` (Fleet's default),
`fl.proposals.regenerate_insert_delete(regenerate=0.75)` (the exact draw sequence of
FormalLanguageTheory-Simple), or `fl.proposals.cpp(body)`, where the body assigns
`std::optional<std::pair<Node,double>> p` from `value` and `&grammar`. For a Lexicon, the
proposal applies to each factor.

## Setup

* `FLEET_ROOT`: defaults to the Fleet tree containing this directory.
* nanobind: `pip install nanobind`, or set `FLEETPY_NANOBIND=/path/to/nanobind` (a git
  checkout with `ext/robin_map`).
* Compiler: flags come from `Fleet.mk` (`-std=c++26`, so g++ ≥ 14). On an older g++,
  fleetpy falls back to `-std=c++2c` or `-std=c++23` and **warns**.
* Cache: `~/.cache/fleetpy/<hostname>/` (or `FLEETPY_CACHE`). The key covers the
  generated source, compiler identity, flags, the Fleet commit **plus uncommitted changes
  under `src/`**, nanobind, and the fleetpy version. Modules built with `-march=native`
  are host-specific, which is why the hostname is part of the path.

## Primitives

| Tier | How | Speed | Threads |
| --- | --- | --- | --- |
| 2 (default) | `@g.primitive` with a Python body (restricted subset) | native | any |
| 1 | `@g.primitive(cpp="expr")` or a block with `return` (escape hatch) | native | any |
| 3 | `@g.primitive(backend="python")` | Python call per evaluation | 1 only |

The format string defaults to `name(%s,%s)` (or `name` for no arguments). Pass `fmt=` to
override. Formats must be unique grammar-wide. `Model.parse` needs every format to be
canonical (`name` / `name(%s,...)`); `serialize()`/`deserialize()` work for any format.

Raise `fl.Undefined` in any tier to make the program undefined on that input (Fleet's
`VMSRuntimeError`).

### Tier-2 semantics (what "transpiled Python" means exactly)

The transpiler rejects anything outside its subset, with the source line. Where Python
and C++ differ, the compiled code follows Python, or the input becomes *undefined*:

* **ints are int64 at every operation.** Overflow anywhere makes the call undefined, even
  if unbounded Python would come back in range later (`a*b - 1` with `a*b = 2^63`). Values
  are range-checked when narrowed to `i8/i16/i32` (arguments, returns, struct fields).
* `//` and `%` round toward −∞; division by zero is undefined; `//` and `%` on floats are
  rejected. `int/int` with an operand beyond ±2^53 is undefined.
* int–float comparisons are rejected (Python compares exactly; write `float(x)`).
* No truthiness: conditions, `and`, `or`, `not` need bools. No bool arithmetic.
* `math.*`: a non-finite result from finite inputs is undefined (where CPython raises).
  `math.hypot` is rejected (CPython doesn't use libm's hypot, so last bits differ).
* Strings: `len` and indexing count code points, not UTF-8 bytes.
* Loops: `for i in range(...)` only, with at most `max_iters` iterations per call
  (default 10^6; `@g.primitive(max_iters=...)`), beyond which the call is undefined.
  Without this, one unbounded loop inside a primitive would hang the sampler.
* Supported beyond arithmetic and comparisons: `if`/`elif`/`else`, conditional expressions,
  `for i in range(...)`, `str` and `list` indexing and slicing (`s[1:]`, `xs[-2:]`; no step),
  `+` on strings, lists and `fl.char`, `len`, `str(c)`, `abs`, `min`, `max`, `int`, `float`,
  bitwise `& | ^ ~ << >>` and `x.bit_count()` / `x.bit_length()` (Python semantics on
  negative ints; a negative shift count is undefined), `math.*`, struct construction and
  fields, calls to other Python primitives, and `raise fl.Undefined`.
* Module-level constants (numbers, bools, strings, and lists or tuples of them) are frozen
  into the C++ at build time. Changing one changes the generated source and so triggers a
  rebuild.
* Variables must be definitely assigned before use. Parameters can't be reassigned.
  Falling off the end is undefined.
* Transpiled code is compiled without FMA contraction (`fp-contract=off`), because
  `-march=native` otherwise fuses `a*b + c` and changes the last bit relative to Python.
  Fleet's own code and tier-1 snippets keep Fleet's flags.

**Every build differential-tests each tier-2 primitive** (600 generated inputs including
edge values) against the user's own Python source, executed by CPython with the int64
rules applied. Any disagreement fails the build. The report (`model.info["differential"]`)
also counts the inputs where int64 semantics differ from unbounded Python
(`int64_limited`), so you can see how often that matters.

## Priors

The default is Fleet's PCFG prior. To change it, write a Python method:

```python
class H(fl.DeterministicHypothesis, grammar=g):
    @fl.prior
    def prior(self) -> float:
        penalty = -25.0 if self.recursion_count() > 0 else math.log1p(-math.exp(-25.0))
        return self.grammar_prior() + penalty
```

`self.grammar_prior()`, `self.recursion_count()` and `self.nodes()` are available. Programs
over `MAX_NODES` still get −∞ first, as in Fleet. Like primitives, the prior is
differential-tested against its Python source on prior samples at build time.

## Existing C++ types

```python
Set = fl.opaque("std::bitset<16>", "<bitset>", py=int,
                from_py="std::bitset<16>((unsigned long long)v)",   # Python int -> C++
                to_py="(int64_t)v.to_ullong()",                     # C++ -> Python int
                check=lambda v: 0 <= v < 2**16)
```

Tier-1 primitives use the C++ type directly. At the Python boundary (data, `call`), values
are converted by a nanobind type caster generated into the model's module. Casters are
compile-time and per-module, so two models can never collide on them. Without `py=`, an
opaque type can be used inside the grammar but can't cross the boundary.

## Likelihoods

Deterministic: `fl.likelihoods.noisy_exact()` (RationalRules), `exact()`, `gaussian(sd)`.
Stochastic: `stochastic_exact(normalize=True)` (log P(y | x)) and
`string_edit(add_p, del_p, alphabet)` (FormalLanguageTheory's `p_delete_append`
likelihood, marginalized over the output distribution). Or a Python method:

* `@fl.likelihood def f(self, di) -> float` is per datum.
* `@fl.likelihood(total=True) def f(self, data) -> float` covers the whole dataset (it replaces
  Fleet's `compute_likelihood`; use `len(data)` and `data[i]`), as Number-Simple needs.
* Inside, use `self.call(x)` (deterministic; an undefined program makes the score −∞),
  `self.call(x, default)` (returns `default` instead, like Fleet's `call(x, err)`), or
  `self.logprob(x, y)` (stochastic).

`cpp(body)` and `cpp_total(body)` are the inline-C++ equivalents.

```python
class H(fl.DeterministicHypothesis, grammar=g):
    @fl.likelihood
    def lik(self, di) -> float:
        y = self.call(di.input)          # undefined program -> this datum scores -inf
        return -(y - di.output) ** 2 / 2.0
```

Fleet's likelihood breakout is only valid if every per-datum term is ≤ 0, and the
`assert` that checks this is compiled out under `-DNDEBUG`. So breakout is **off** unless
the likelihood declares `nonpositive=True` (the library likelihoods know their own).

## Running

`model.run(data, algo, control=fl.Control(...), top=N)`, where `algo` is
`MCMCChain()`, `ChainPool(n)` or `ParallelTempering(n, max_temp)`. The run holds no GIL,
keeps Fleet's busy-spin sample consumer, and returns a `TopN` plus stats. Ctrl-C stops
the run and returns partial results (`stats.interrupted`). `stats.reproducible` is true
only for a seeded, single-threaded, step-budgeted run without a PT swapper thread; such
runs reproduce bit-for-bit, including across repeated runs in one process.

## Checks (`fleetpy.check`)

* `roundtrip(model)`: `parse(string())` and `deserialize(serialize())` are identities.
* `prior(model)`: Fleet's log prior vs an independent Python PCFG computation.
* `posterior(model, data, max_nodes=k)`: one chain's visit frequencies vs the exact
  posterior over all programs with ≤ k nodes (MAX_NODES = k makes that the exact
  target). Per-program z-scores with batch-means standard errors.
* `run_checked(script)`: runs a script with every model built under ASan+UBSan.
  One known upstream Fleet report is suppressed narrowly (`vptr_check:Node`, from
  `BaseNode.h:252`); see `check.KNOWN_FLEET_UBSAN`.

## Ejecting

`model.eject("Models/MyModel", data, algo)` writes a standalone `Main.cpp` (the same
model code, with the data embedded as C++ literals) and a `Makefile` that includes
`Fleet.mk`. Models with Python-callback primitives can't be ejected.

## Floating point and native C++

Compiled Python is never contracted into fused multiply-adds, because that would change
results relative to Python. Fleet's own builds allow contraction under `-march=native`. A
float expression of the form `a*b + c`, written in Python, can therefore differ in the last
bit from the same expression hand-written in C++ and compiled by Fleet. None of the four
examples hit this (they remain bit-identical), but other models might.

## Known Fleet issues (worked around in generated code)

* `src/VirtualMachine/Program/Program.h`: `ProgramLoader::was_called` has no initializer, so
  every freshly constructed hypothesis holds an indeterminate `bool` that copies and moves
  then read. That is undefined behavior; UBSan reported a moved `Lexicon` loading the value
  16. fleetpy's generated classes initialize it. The upstream fix is one line:
  `bool was_called = false;`.
* `src/BaseNode.h:252`: a downcast to `Node` inside `BaseNode`'s constructor. It is suppressed
  narrowly in checked runs (`check.KNOWN_FLEET_UBSAN`).

## Not yet implemented

The streaming `samples()` iterator, aggregators beyond TopN, `cpp_total` likelihoods for a
Lexicon, and adding or removing Lexicon factors during MCMC (Fleet's Lexicon proposals don't
either).

## Tests

```
cd Fleet/fleetpy && python3 -m pytest -q tests           # ~10 min; add -m "not slow" to skip ASan
```

Bit-for-bit equivalence tests (identical top-N posteriors and identical hash over every
sample of the chain, for the same seed):

* `Models/RationalRules`, 100k samples
* `Models/Number-Simple`, 100k samples
* `Models/FormalLanguageTheory-Simple`, 30k samples
* a hand-written Fleet Lexicon (`tests/native/lex_driver.cpp`), 100k samples

Exact-posterior tests cover deterministic, stochastic (including Fleet's insert/delete
proposals), and lexicon models, and all model kinds are run under ASan + UBSan.
