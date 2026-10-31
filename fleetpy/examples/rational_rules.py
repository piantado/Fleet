"""
RationalRules (Models/RationalRules/Main.cpp) written with fleetpy.

Objects have shape, color and size, each in {0,1,2}, numbered as in Fleet's
ShapeColorSizeObject enums:
  Shape: Rectangle=0, Triangle=1, Circle=2
  Color: Blue=0, Yellow=1, Green=2
  Size:  size1=0, size2=1, size3=2
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # run from anywhere in the repo

import fleetpy as fl  # noqa: E402
from fleetpy import builtins as B  # noqa: E402


@fl.struct
class Obj:
    shape: fl.i8
    color: fl.i8
    size: fl.i8


g = fl.Grammar(input=Obj, output=bool, types=[Obj, bool])

# Same rules and probabilities as the C++ model; the bodies are plain Python,
# compiled to C++ (and differential-tested against this source on every build).
@g.primitive
def blue(x: Obj) -> bool:
    return x.color == 0

@g.primitive
def yellow(x: Obj) -> bool:
    return x.color == 1

@g.primitive
def green(x: Obj) -> bool:
    return x.color == 2

@g.primitive
def rectangle(x: Obj) -> bool:
    return x.shape == 0

@g.primitive
def triangle(x: Obj) -> bool:
    return x.shape == 1

@g.primitive
def circle(x: Obj) -> bool:
    return x.shape == 2

@g.primitive
def size1(x: Obj) -> bool:
    return x.size == 0

@g.primitive
def size2(x: Obj) -> bool:
    return x.size == 1

@g.primitive
def size3(x: Obj) -> bool:
    return x.size == 2

g.add_builtin(B.And, B.Or, B.Not, B.X)


class MyHypothesis(fl.DeterministicHypothesis, grammar=g):
    likelihood = fl.likelihoods.noisy_exact()


# The data from Models/RationalRules/Main.cpp
DATA = [
    fl.Datum(Obj(1, 0, 0), True, 0.75),   # triangle-blue-1
    fl.Datum(Obj(2, 0, 0), False, 0.75),  # circle-blue-1
    fl.Datum(Obj(2, 1, 0), False, 0.75),  # circle-yellow-1
]

if __name__ == "__main__":
    model = fl.build(MyHypothesis)
    res = model.run(DATA, fl.ParallelTempering(chains=4, max_temp=10.0),
                    control=fl.Control(time="2s", threads=2, seed=1), top=10)
    for h in res.top:
        print(f"{h.posterior:.4f}\t{h.prior:.4f}\t{h.likelihood:.4f}\t{h}")
    print(res.stats)
