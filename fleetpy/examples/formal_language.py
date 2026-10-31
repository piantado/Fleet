"""
Models/FormalLanguageTheory-Simple written with fleetpy: a stochastic hypothesis (flip()),
recursion (F, and memoized Fm), constant terminals, Fleet's string-edit likelihood, and the
model's own regenerate/insert/delete proposal.

Every primitive is plain Python, compiled to C++. They compute the same results as the C++
model's in-place add_vms versions (strings here are ASCII, so Python's code points and
C++'s bytes agree).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # run from anywhere in the repo

import fleetpy as fl  # noqa: E402
from fleetpy import Undefined, builtins as B  # noqa: E402

ALPHABET = "01"
MAX_LENGTH = 64            # longest string append/pair will build (else the path errors)

g = fl.Grammar(input=str, output=str, types=[str, fl.char, bool])


@g.primitive
def tail(s: str) -> str:
    return s[1:]


@g.primitive
def head(s: str) -> str:
    return s[:1]


@g.primitive
def append(a: str, b: str) -> str:
    if len(a) + len(b) > MAX_LENGTH:
        raise Undefined
    return a + b


@g.primitive
def pair(a: str, b: fl.char) -> str:
    if len(a) + 1 > MAX_LENGTH:
        raise Undefined
    return a + b


@g.primitive
def eq(x: str, y: str) -> bool:
    return x == y


g.add_terminal("Ø", "", p=3.0)
g.add_builtin(B.And, B.Or, B.Not,
              B.X(p=3.0),
              B.If(str, fmt="if_s(%s,%s,%s)"),
              B.If(fl.char, fmt="if_c(%s,%s,%s)"),
              B.Flip(p=3.0),
              B.Recurse(fmt="F(%s)"),
              B.MemRecurse(fmt="Fm(%s)"))

for c in ALPHABET:
    g.add_terminal("'" + c + "'", c, fl.char, p=3.0 / len(ALPHABET))


class MyHypothesis(fl.StochasticHypothesis, grammar=g):
    likelihood = fl.likelihoods.string_edit(add_p=0.01, del_p=0.01, alphabet=ALPHABET)
    proposal = fl.proposals.regenerate_insert_delete(regenerate=0.75)
    vm = fl.VMLimits(max_steps=256, max_outputs=256)   # as the C++ model's main() sets


# The C++ model's default data: thunks (no input) producing these strings.
DATA = [fl.Datum("", s) for s in "01,01001,010010001,01001000100001".split(",")]

if __name__ == "__main__":
    model = fl.build(MyHypothesis)
    res = model.run(DATA, fl.ParallelTempering(chains=4, max_temp=1.2),
                    control=fl.Control(time="20s", threads=2, seed=1), top=5)
    for h in res.top:
        d = h.distribution("")
        best = sorted(d.items(), key=lambda kv: -kv[1])[:4]
        print(f"{h.posterior:.3f}\t{h}\n\t{best}")
