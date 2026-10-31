"""
A two-factor Lexicon with mutual recursion (fleetpy). Factor 1 is the one a call runs; it can
call factor 0 (or itself) through F(key, arg), or the memoized Fm(key, arg).

The data are pairs (x, 2x + 1): one solution is F1(x) := inc(F(0,x)), F0(x) := add(x,x).
Everything is plain Python compiled to C++; tests/native/lex_driver.cpp is the same model
written by hand in Fleet C++.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # run from anywhere in the repo

import fleetpy as fl  # noqa: E402
from fleetpy import builtins as B  # noqa: E402

g = fl.Grammar(input=int, output=int, types=[int, bool])


g.add_terminal("zero", 0, p=2.0)


@g.primitive
def inc(a: int) -> int:
    return a + 1


@g.primitive
def dec(a: int) -> int:
    return a - 1


@g.primitive
def add(a: int, b: int) -> int:
    return a + b


@g.primitive
def iszero(a: int) -> bool:
    return a == 0


g.add_builtin(B.X(p=3.0),
              B.If(int, fmt="if(%s,%s,%s)"),
              B.LexiconRecurse(key=int, fmt="F(%s,%s)"),
              B.LexiconMemRecurse(key=int, fmt="Fm(%s,%s)"))


class Lex(fl.Lexicon, grammar=g, keys=[0, 1], main=1):
    likelihood = fl.likelihoods.noisy_exact()


DATA = [fl.Datum(x, 2 * x + 1, 0.9) for x in range(5)]

if __name__ == "__main__":
    model = fl.build(Lex)
    res = model.run(DATA, fl.ChainPool(4), control=fl.Control(time="10s", threads=2, seed=1), top=5)
    for h in res.top:
        print(f"{h.posterior:.3f}\t{h}")
    best = res.top[0]
    print([best.call(x) for x in range(8)])
