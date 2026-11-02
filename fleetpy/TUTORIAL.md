# A fleetpy tutorial

This walks through one small model from a blank file to a running sampler, and then
through the things you'll want next: inspecting hypotheses, stochastic programs, lexicons,
and checking that it all does what you think. Every snippet here runs as written; the
whole thing lives in `examples/tutorial.py` if you'd rather read it in one piece.

fleetpy's job is to let you *write* a Fleet model in Python while Fleet *runs* it as
compiled C++. You describe types, primitives, a grammar, a hypothesis and data; fleetpy
writes the same C++ you would have written by hand, compiles it once (then caches it), and
runs inference with no Python in the loop.

## 0. Setup

```bash
pip install nanobind            # builds the Python modules
export PYTHONPATH=/path/to/Fleet/fleetpy
```

That's it: fleetpy finds Fleet itself through the tree it lives in. The first build of a
model takes 10–20 seconds; after that an unchanged model loads instantly.

## 1. The problem

Suppose a learner sees numbers labeled *yes* or *no* and must guess the rule:

```
 2 → yes    3 → no    4 → yes    7 → no    10 → yes    9 → no
```

We'll give the learner a little language of number predicates, a prior over programs in
that language (simpler is likelier), and a likelihood that tolerates a bit of noise. Fleet
then samples programs from the posterior.

## 2. Types and primitives

```python
from fleetpy import *

g = Grammar(input=int, output=bool)

@g.primitive
def even(x: int) -> bool:
    return x % 2 == 0

@g.primitive
def gt(x: int, y: int) -> bool:
    return x > y

@g.primitive
def half(x: int) -> int:
    return x // 2

@g.primitive
def succ(x: int) -> int:
    return x + 1

g.add_terminal("one", 1)
g.add_terminal("two", 2)
g.add_builtin(X, And, Or, Not)
```

A few things to notice.

**Primitives are ordinary annotated Python.** The annotations are the grammar's type
system: `even` is a rule that turns an `int` into a `bool`. fleetpy compiles the body to
C++, and on every build it runs the compiled version and your Python side by side on a few
hundred generated inputs (including nasty ones: 0, −1, 2⁶³−1, …). If they ever disagree, the
build fails. So you can trust that what runs in C++ is what you wrote.

**The semantics are Python's, not C's.** `x % 2` is Python's modulo (never negative for a
positive divisor); `x // 2` rounds toward −∞; `x + 1` on the largest int64 doesn't wrap
around, it makes the program *undefined* on that input. The full rules are in the README
under "Tier-2 semantics", but the short version is: if Python would raise, the program is
undefined; if the result wouldn't fit in 64 bits, the program is undefined; otherwise it's
exactly what Python computes.

**`X` is the program's argument**, `And`/`Or`/`Not` are Fleet's own short-circuiting
builtins, and the terminals are constants. Every rule prints the way Fleet prints it:
`even(%s)`, `gt(%s,%s)`, `x`, `and(%s,%s)`, `one`. (Pass `fmt=` to a primitive to change
that.)

## 3. The hypothesis

```python
class Rule(DeterministicHypothesis, grammar=g):
    likelihood = likelihoods.noisy_exact()
```

A hypothesis is a program from the grammar, with a prior and a likelihood. The prior is
Fleet's PCFG prior by default (every rule's `p=` weight, normalized within its type).
`noisy_exact()` is RationalRules' likelihood: with probability `reliability` the label is
the program's output, otherwise a coin flip. The reliability lives on each datum:

```python
data = [Datum(2, True, 0.9), Datum(3, False, 0.9), Datum(4, True, 0.9),
        Datum(7, False, 0.9), Datum(10, True, 0.9), Datum(9, False, 0.9)]
```

## 4. Build and run

```python
model = build(Rule)
res = model.run(data, ParallelTempering(chains=4, max_temp=5.0),
                control=Control(steps=50_000, seed=1), top=5)
for h in res.top:
    print(f"{h.posterior:8.3f}  {h}")
```

You'll see something like

```
  -3.709  λx.even(x)
  -6.928  λx.not(not(even(x)))
  -7.110  λx.not(even(succ(x)))
  -7.292  λx.even(succ(succ(x)))
  ...
```

`build` generated and compiled the C++ (look in `~/.cache/fleetpy/` if you're curious:
`model.cpp` is readable Fleet code, and `build.log` is the compiler's output). `run`
released the GIL and ran parallel tempering in C++. `res.top` is Fleet's `TopN`, best
first; `res.stats` has the sample count, acceptance ratios and timing.

`Control` takes `steps=` or `time="30s"`, `threads=`, and `seed=`. A seeded, single-threaded,
step-budgeted run is reproducible bit for bit, and `res.stats.reproducible` tells you
whether yours was.

## 5. Looking at hypotheses

A hypothesis prints as Fleet prints it, and you can run it:

```python
best = res.top[0]
print(best.string())            # λx.even(x)
print(best.call(6), best.call(7))   # True False
print(best.prior, best.likelihood, best.posterior)
```

You can also go the other way, from text to a hypothesis, which is handy for testing a
specific program or for setting up data:

```python
h = model.parse("and(even(x),gt(x,two))")
print([h.call(n) for n in range(1, 7)])          # [False, False, False, True, False, True]
print(h.compute_posterior(data))                 # (prior, likelihood, posterior)
```

`parse` type-checks the program against the grammar in Python, so a typo is a clear error
(`no rule with format 'evne(%s)'`, `gt returns bool but int is needed here`) rather than a
crash in C++.

A primitive can declare that it has no value on some input, which makes any program
using it there *undefined*; `call` then raises `Undefined`, or returns a default if you
give one:

```python
@g.primitive
def recip(x: int) -> int:
    if x == 0:
        raise Undefined        # the program has no value here
    return 100 // x

model.parse("even(recip(x))").call(0)                     # raises Undefined
model.parse("even(recip(x))").call(0, default="no value")
```

(Python's own `ZeroDivisionError`, `IndexError` and int64 overflow mean the same thing,
so `return 100 // x` alone would behave identically; `raise Undefined` just says it out loud.)

`serialize()` / `model.deserialize()` round-trip a hypothesis exactly, whatever its rules
are named; `str()` is for humans.

## 6. Writing the prior and likelihood in Python

Library likelihoods cover the common cases (`exact()`, `noisy_exact()`, `gaussian(sd)`),
but you can write your own as a method, in the same Python subset as primitives. `self.call`
runs the program:

```python
class Rule2(DeterministicHypothesis, grammar=g):
    @likelihood
    def lik(self, di) -> float:
        y = self.call(di.input)             # undefined program -> this datum scores -inf
        return 0.0 if y == di.output else -2.0

    @prior
    def p(self) -> float:
        return self.grammar_prior() - 0.5 * self.nodes()     # extra penalty on size
```

`@likelihood(total=True)` gives you the whole dataset at once (`len(data)`, `data[i]`) for
likelihoods that aren't a sum over datums. Like primitives, these are differential-tested
against your Python on every build; the report is in `model.info["differential"]`.

## 7. Stochastic programs

A program can flip coins. Then it doesn't have *an* output on an input, it has a
distribution over outputs, and the hypothesis class changes to say so:

```python
gc = Grammar(input=bool, output=bool)
gc.add_terminal("T", True)
gc.add_builtin(X, Flip, And, Or, Not)        # Flip is flip(): a fair coin

class Coin(StochasticHypothesis, grammar=gc):
    likelihood = likelihoods.stochastic_exact()  # log P(output = y | x)

coin = build(Coin)
h = coin.parse("or(flip(),flip())")
print(h.distribution(True))                  # {True: 0.75, False: 0.25} (up to rounding)
print(h.logprob(True, False))                # log(0.25)
```

Fleet runs *every* execution path (`Flip` forks the virtual machine) and sums their
probabilities. `VMLimits` on the class controls how deep that search goes; the defaults are
Fleet's. Using `Flip` in a `DeterministicHypothesis` is a build-time error, not a crash.

## 8. Recursion and lexicons

A program can call itself through `Recurse` (and `MemRecurse`, which memoizes):

```python
gs = Grammar(input=str, output=str, types=[str, bool])

@gs.primitive
def tail(s: str) -> str:
    return s[1:]

@gs.primitive
def head(s: str) -> str:
    return s[:1]

@gs.primitive
def cat(a: str, b: str) -> str:
    if len(a) + len(b) > 64:
        raise Undefined
    return a + b

@gs.primitive
def empty(s: str) -> bool:
    return len(s) == 0

gs.add_terminal("Ø", "")
gs.add_builtin(X, If(str), Recurse(fmt="F(%s)"))

class Rev(DeterministicHypothesis, grammar=gs):
    likelihood = likelihoods.exact()

rev = build(Rev)
print(rev.parse("if(empty(x),Ø,cat(F(tail(x)),head(x)))").call("hello"))   # olleh
```

Recursion deeper than Fleet's `MAX_RECURSE` (64, settable with `VMLimits`) makes the
program undefined rather than overflowing the stack.

A **lexicon** is several programs that call each other by name. `main=` is the one a
call runs; the others are reached through `LexiconRecurse(key=...)`:

```python
gl = Grammar(input=int, output=int, types=[int, bool])
gl.add_terminal("zero", 0)

@gl.primitive
def inc(a: int) -> int:
    return a + 1

@gl.primitive
def add(a: int, b: int) -> int:
    return a + b

gl.add_builtin(X, LexiconRecurse(key=int, fmt="F(%s,%s)"))

class Lex(Lexicon, grammar=gl, keys=[0, 1], main=1):
    likelihood = likelihoods.noisy_exact()

lex = build(Lex)
h = lex.parse({0: "add(x,x)", 1: "inc(F(0,x))"})   # F(0, x) calls factor 0
print(h.call(5), h.factors)                           # 11 {0: 'λx.add(x,x)', 1: 'λx.inc(F(0,x))'}
```

## 9. Trusting it

Besides the differential tests that run on every build, `fleetpy.check` has:

```python
from fleetpy import check

check.roundtrip(model)          # parse(str(h)) == h, deserialize(serialize(h)) == h, on prior samples
check.prior(model)              # Fleet's log prior vs an independent Python computation
check.posterior(model, data, max_nodes=4)
```

The last one is the strong one. It enumerates every program with at most `max_nodes` nodes,
computes the exact posterior over them, runs one chain restricted to that space, and tests
the chain's visit frequencies against the exact answer (a chi-square over per-program
z-scores with batch-means standard errors). It returns a report with the p-value and
total-variation distance, and raises `CheckFailed` if the sampler isn't sampling the
posterior you declared. If you write a custom proposal, run this.

## 10. Leaving Python

`model.eject("Models/MyRule", data, ParallelTempering(8, 10.0))` writes a standalone
`Main.cpp` and `Makefile`, exactly like the models in Fleet's `Models/` directory, with your
data embedded. From there it's a normal Fleet model.

## Where to look next

- `README.md`: the full reference, including exactly which Python the transpiler accepts
  and how fleetpy is put together.
- `examples/`: Fleet's RationalRules, Number, FormalLanguageTheory and a lexicon, each
  tested to be bit-identical to the C++ model.
- `tests/`: the spec, in executable form.
