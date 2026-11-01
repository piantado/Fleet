"""
fleetpy: declare Fleet models in Python; run them as compiled Fleet C++.

    from fleetpy import *

    @struct
    class Obj:
        shape: i8
        color: i8

    g = Grammar(input=Obj, output=bool)

    @g.primitive
    def blue(x: Obj) -> bool:
        return x.color == 0
    g.add_builtin(And, Or, Not, X)

    class H(DeterministicHypothesis, grammar=g):
        likelihood = likelihoods.noisy_exact()

    model = build(H)
    res = model.run(data, ParallelTempering(8, 10.0), control=Control(time="10s", threads=8))

``import fleetpy as fl`` and ``fl.`` prefixes work equally; ``__all__`` is exactly the
model-writing vocabulary, so a star import adds nothing else to your module.

The pipeline: a model *declaration* (types, Grammar, hypothesis class) is resolved into a
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
from .rules import (BUILTINS, And, Flip, FlipP, If, Iff, Implies, LexiconMemRecurse, LexiconRecurse,
                    LexiconSafeMemRecurse, LexiconSafeRecurse, MemRecurse, Not, Or, Recurse, SafeFlipP,
                    SafeMemRecurse, SafeRecurse, Sample, X, builtins, default_format)

B = builtins   # the conventional short name: B.And, B.If(int), ...

__all__ = [
    # declaring
    "Grammar", "struct", "newtype", "opaque", "default_format",
    "boolean", "char", "f64", "i8", "i16", "i32", "i64", "string",
    "DeterministicHypothesis", "StochasticHypothesis", "Lexicon", "VMLimits",
    "likelihood", "likelihoods", "prior", "proposals", "Undefined",
    # Fleet's builtins, as a namespace and by name
    "B", *[s.name for s in BUILTINS],
    # running
    "build", "Datum", "Control", "MCMCChain", "ChainPool", "ParallelTempering",
    "Model", "Hypothesis", "RunResult", "BuildError", "config",
]
