"""
fleetpy: declare Fleet models in Python; run them as compiled Fleet C++.

    import fleetpy as fl
    g = fl.Grammar(input=Obj, output=bool)
    @g.primitive(cpp="x.color == 0")
    def blue(x: Obj) -> bool: ...
    ...
    model = fl.build(H)
    res = model.run(data, fl.ParallelTempering(8, 10.0), control=fl.Control(time="10s", threads=8))

See fleetpy/README.md (in the Fleet repository) for details.
"""

from . import likelihoods
from .build import BuildError, FLEETPY_VERSION as __version__, config
from .grammar import Grammar, builtins, default_format
from .hypothesis import (ChainPool, Control, DeterministicHypothesis, Lexicon, MCMCChain, ParallelTempering,
                         StochasticHypothesis, VMLimits, proposals)
from .model import Datum, Hypothesis, Model, RunResult, Undefined, build
from .transpile import likelihood, prior
from .ftypes import (boolean, char, f64, i8, i16, i32, i64, newtype, opaque, string, struct)

__all__ = [
    "BuildError", "ChainPool", "Control", "Datum", "DeterministicHypothesis", "Grammar",
    "Hypothesis", "Lexicon", "MCMCChain", "Model", "ParallelTempering", "RunResult", "StochasticHypothesis", "Undefined", "VMLimits",
    "boolean", "build", "char", "builtins", "config", "default_format", "f64", "i8", "i16", "i32",
    "i64", "likelihood", "likelihoods", "prior", "newtype", "proposals", "opaque", "string", "struct",
]
