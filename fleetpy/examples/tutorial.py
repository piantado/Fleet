"""
The code from TUTORIAL.md, in one runnable file. Run it from anywhere:

    python3 examples/tutorial.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fleetpy import *  # noqa: E402,F403
from fleetpy import check  # noqa: E402

# ---- 2. types and primitives ---------------------------------------------------------------
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


@g.primitive
def recip(x: int) -> int:
    if x == 0:
        raise Undefined        # the program has no value here
    return 100 // x


g.add_terminal("one", 1)
g.add_terminal("two", 2)
g.add_builtin(X, And, Or, Not)


# ---- 3. the hypothesis ------------------------------------------------------------------------
class Rule(DeterministicHypothesis, grammar=g):
    likelihood = likelihoods.noisy_exact()


data = [Datum(2, True, 0.9), Datum(3, False, 0.9), Datum(4, True, 0.9),
        Datum(7, False, 0.9), Datum(10, True, 0.9), Datum(9, False, 0.9)]


# ---- 6. Python prior and likelihood ---------------------------------------------------------
class Rule2(DeterministicHypothesis, grammar=g):
    @likelihood
    def lik(self, di) -> float:
        y = self.call(di.input)             # undefined program -> this datum scores -inf
        return 0.0 if y == di.output else -2.0

    @prior
    def p(self) -> float:
        return self.grammar_prior() - 0.5 * self.nodes()     # extra penalty on size


# ---- 7. stochastic programs -------------------------------------------------------------------
gc = Grammar(input=bool, output=bool)
gc.add_terminal("T", True)
gc.add_builtin(X, Flip, And, Or, Not)


class Coin(StochasticHypothesis, grammar=gc):
    likelihood = likelihoods.stochastic_exact()


# ---- 8. recursion -----------------------------------------------------------------------------
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


# ---- 8. a lexicon -----------------------------------------------------------------------------
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


if __name__ == "__main__":
    # 4. build and run
    model = build(Rule)
    res = model.run(data, ParallelTempering(chains=4, max_temp=5.0),
                    control=Control(steps=50_000, seed=1), top=5)
    for h in res.top:
        print(f"{h.posterior:8.3f}  {h}")

    # 5. looking at hypotheses
    best = res.top[0]
    print(best.string(), best.call(6), best.call(7))
    print(best.prior, best.likelihood, best.posterior)
    h = model.parse("and(even(x),gt(x,two))")
    print([h.call(n) for n in range(1, 7)])
    print(h.compute_posterior(data))
    try:
        model.parse("even(recip(x))").call(0)
    except Undefined as e:
        print("undefined:", e)
    print(model.parse("even(recip(x))").call(0, default="no value"))

    model2 = build(Rule2)
    print("differential:", model2.info["differential"]["likelihood"], model2.info["differential"]["prior"])

    # 7. stochastic
    coin = build(Coin)
    h = coin.parse("or(flip(),flip())")
    print(h.distribution(True), h.logprob(True, False))

    # 8. recursion and lexicons
    rev = build(Rev)
    print(rev.parse("if(empty(x),Ø,cat(F(tail(x)),head(x)))").call("hello"))
    lex = build(Lex)
    h = lex.parse({0: "add(x,x)", 1: "inc(F(0,x))"})
    print(h.call(5), h.factors)

    # 9. checks
    print(check.roundtrip(model), check.prior(model)["max_rel_error"])
    print(check.posterior(model, data, max_nodes=4, steps=100_000)["p_value"])
