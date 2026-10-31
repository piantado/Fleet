"""
Models/Number-Simple written with fleetpy: the number-word learning model (Piantadosi,
Tenenbaum & Goodman 2012), with recursion, a recursion penalty in the prior, two
same-format `if` rules for different types, and a likelihood that ignores the data and
scores the knower-level predictions directly.

Everything is plain Python compiled to C++. A set of up to 16 objects is an int whose
bits are the members (the C++ model uses std::bitset<16>); `Set` is a distinct type so the
grammar keeps sets and number words apart.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # run from anywhere in the repo

import fleetpy as fl                      # noqa: E402
from fleetpy import Undefined, builtins as B   # noqa: E402

U = -999                                  # "undefined" word
FULL = 0xFFFF                             # all 16 bits: complement is within these
ALPHA = 0.9
NDATA = 500.0
RECURSION_PENALTY = -25.0
# probability of each set size 0,1,2,...
NP = [0.000000000, 0.683564771, 0.141145140, 0.056400989, 0.031767168, 0.028248050, 0.015693361,
      0.014361803, 0.008179570, 0.009986684, 0.010652463]

Set = fl.newtype("Set", fl.i32)
Word = fl.i32                             # C++ int, as `using word = int` in the C++ model

g = fl.Grammar(input=Set, output=Word, types=[bool, Word, Set])

g.add_terminal("undef", U, Word)
for _i, _name in enumerate(["one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"], 1):
    g.add_terminal(_name, _i, Word, p=0.1)


@g.primitive
def next(w: Word) -> Word:
    return U if w == U else w + 1


@g.primitive
def prev(w: Word) -> Word:
    return U if (w == U or w == 1) else w - 1


@g.primitive(p=2.0)
def singleton(s: Set) -> bool:
    return s.bit_count() == 1


@g.primitive(p=2.0)
def doubleton(s: Set) -> bool:
    return s.bit_count() == 2


@g.primitive(p=2.0)
def tripleton(s: Set) -> bool:
    return s.bit_count() == 3


@g.primitive
def select(s: Set) -> Set:
    """The singleton of the first member (the lowest set bit)."""
    if s == 0:
        raise Undefined
    return s & -s


@g.primitive(fmt="union(%s,%s)")
def union_(x: Set, y: Set) -> Set:
    return x | y


@g.primitive
def setdifference(x: Set, y: Set) -> Set:
    return x & ~y


@g.primitive
def intersection(x: Set, y: Set) -> Set:
    return x & y


@g.primitive
def complement(x: Set, y: Set) -> Set:    # as in the C++ model: ignores y
    return ~x & FULL


g.add_builtin(B.And(p=1 / 3), B.Or(p=1 / 3), B.Not(p=1 / 3),
              B.X(p=25.0),
              B.If(Set, p=1 / 2), B.If(Word, p=1 / 2),   # same format, different types
              B.Recurse(fmt="recurse(%s)"))


class MyHypothesis(fl.DeterministicHypothesis, grammar=g):

    @fl.prior
    def prior(self) -> float:
        if self.recursion_count() > 0:
            return self.grammar_prior() + RECURSION_PENALTY
        return self.grammar_prior() + math.log1p(-math.exp(RECURSION_PENALTY))

    # As in the original paper: average over set sizes 1..9, weighted by how often each size
    # occurs; an undefined answer counts as no prediction.
    @fl.likelihood(total=True)
    def lik(self, data) -> float:
        ll = 0.0
        for x in range(1, 10):
            v = self.call((1 << x) - 1, U)      # U if the program is undefined on this set
            if v == U:
                p = 1.0 - ALPHA
            else:
                p = (1.0 - ALPHA) / 10.0 + (ALPHA if v == x else 0.0)
            ll += NDATA * NP[x] * math.log(p)
        return ll


def make_set(n: int) -> int:
    return (1 << n) - 1


def knower_level(h) -> str:
    out = []
    for n in range(1, 11):
        v = h.call(make_set(n), default=U)
        out.append("U" if v == U else str(v))
    return ".".join(out)


if __name__ == "__main__":
    model = fl.build(MyHypothesis)
    res = model.run([], fl.ParallelTempering(chains=4, max_temp=5.0),
                    control=fl.Control(time="20s", threads=2, seed=1), top=5)
    for h in res.top:
        print(f"{h.posterior:.3f}\t{knower_level(h)}\t{h}")
