"""
fleetpy: declare Fleet models in Python; run them as compiled Fleet C++.

    import fleetpy as fl
    from fleetpy import builtins as B

    g = fl.Grammar(input=Obj, output=bool)

    @g.primitive
    def blue(x: Obj) -> bool:
        return x.color == 0
    g.add_builtin(B.And, B.Or, B.Not, B.X)

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.noisy_exact()

    model = fl.build(H)
    res = model.run(data, fl.ParallelTempering(8, 10.0), control=fl.Control(time="10s", threads=8))

The pipeline: a model *declaration* (types, grammar, hypothesis class) is resolved into a
ModelSpec (hypothesis.specify, which compiles Python bodies with transpile), turned into
C++ (codegen, plus the generic C++ in fleetpy/cpp), compiled and cached (build), and
wrapped as a Model. See fleetpy/README.md (in the Fleet repository).
"""

from . import likelihoods
from .build import FLEETPY_VERSION as __version__, BuildError, config
from .ftypes import boolean, char, f64, i8, i16, i32, i64, newtype, opaque, string, struct
from .grammar import Grammar
from .hypothesis import (DeterministicHypothesis, Lexicon, StochasticHypothesis, VMLimits, likelihood,
                         prior, proposals)
from .inference import ChainPool, Control, MCMCChain, ParallelTempering
from .model import Datum, Hypothesis, Model, RunResult, Undefined, build
from .rules import builtins, default_format

__all__ = [
    "BuildError", "ChainPool", "Control", "Datum", "DeterministicHypothesis", "Grammar",
    "Hypothesis", "Lexicon", "MCMCChain", "Model", "ParallelTempering", "RunResult", "StochasticHypothesis",
    "Undefined", "VMLimits", "boolean", "build", "builtins", "char", "config", "default_format", "f64",
    "i8", "i16", "i32", "i64", "likelihood", "likelihoods", "newtype", "opaque", "prior", "proposals",
    "string", "struct",
]
