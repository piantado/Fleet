"""
The compiled model: the Python face of one generated module.
"""

from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass
from pathlib import Path

from . import codegen, parsing
from .build import build_module, fleet_root
from .hypothesis import ModelSpec, specify
from .inference import Control, ParallelTempering, Sampler


class Undefined(Exception):
    """Raise inside a primitive (any tier) to make the program's output undefined on this
    input. Compiled code maps it to Fleet's VMSRuntimeError, which the VM catches."""


@dataclass(frozen=True)
class Datum:
    """Mirrors Fleet's defaultdatum_t."""
    input: object
    output: object
    reliability: float = math.nan
    count: float = 1.0


class Hypothesis:
    """A handle to a compiled hypothesis, with Python-typed accessors."""

    __slots__ = ("_h", "_model")

    def __init__(self, h, model):
        self._h, self._model = h, model

    prior = property(lambda self: self._h.prior)
    likelihood = property(lambda self: self._h.likelihood)
    posterior = property(lambda self: self._h.posterior)
    nodes = property(lambda self: self._h.nodes)

    def string(self) -> str:
        return self._h.string()

    def serialize(self) -> str:
        """Exact and unambiguous for any formats: Fleet's parseable form, or for a Lexicon,
        JSON [[key, parseable], ...] in key order."""
        if self._model.spec.is_lexicon:
            return json.dumps([[k, p] for k, p in self._h.factors()])
        return self._h.parseable()

    @property
    def factors(self) -> dict:
        """Lexicon only: {key: printed program of that factor}."""
        if not self._model.spec.is_lexicon:
            raise AttributeError("factors is only defined for a Lexicon")
        return dict(self._h.factor_strings())

    def call(self, x, *, default=Undefined):
        """Deterministic: the program's output on ``x``; raises fleetpy.Undefined on a
        runtime error unless default= is given. Stochastic: {output: log mass} over all
        execution paths, Fleet's unnormalized DiscreteDistribution (see distribution())."""
        m = self._model
        g = m.grammar
        xc = g.input.to_cpp(x, m.module)
        if m.spec.stochastic:
            return {g.output.from_cpp(o): lp for o, lp in self._h.call(xc)}
        ok, out = self._h.call(xc)
        if ok:
            return g.output.from_cpp(out)
        if default is Undefined:
            raise Undefined(f"{self.string()} is undefined on {x!r}")
        return default

    def distribution(self, x, *, normalize: bool = True) -> dict:
        """Stochastic only: {output: probability}, normalized by the completed mass if asked."""
        if not self._model.spec.stochastic:
            raise TypeError("distribution() is for stochastic hypotheses; use call()")
        d = self.call(x)
        if not d:
            return {}
        mx = max(d.values())
        z = mx + math.log(math.fsum(math.exp(v - mx) for v in d.values())) if normalize else 0.0
        return {k: math.exp(v - z) for k, v in d.items()}

    def logprob(self, x, y) -> float:
        """Stochastic only: log P(output = y | x), normalized: the value fleetpy's
        likelihoods (and self.logprob in an @fl.likelihood) use."""
        m = self._model
        if not m.spec.stochastic:
            raise TypeError("logprob() is for stochastic hypotheses")
        return self._h.logprob(m.grammar.input.to_cpp(x, m.module), m.grammar.output.to_cpp(y, m.module))

    def compute_posterior(self, data):
        """Recompute (prior, likelihood, posterior) on ``data`` in C++."""
        return self._h.compute_posterior(self._model._convert_data(data))

    def __str__(self):
        return self.string()

    def __repr__(self):
        return f"<Hypothesis {self.string()} posterior={self.posterior:.6g}>"

    def __eq__(self, other):
        return isinstance(other, Hypothesis) and self._h == other._h

    def __hash__(self):
        return self._h.__hash__()


@dataclass
class RunStats:
    samples: int
    posterior_calls: int
    seconds: float
    interrupted: bool
    acceptance: list
    seed: int
    threads: int
    reproducible: bool
    breakout: bool
    control: str


@dataclass
class RunResult:
    top: list
    stats: RunStats

    def __repr__(self):
        best = self.top[0] if self.top else None
        return f"<RunResult top={len(self.top)} best={best!r} samples={self.stats.samples}>"


class Model:

    def __init__(self, spec: ModelSpec, module, info):
        self.spec, self.module, self.info = spec, module, info
        self.grammar = spec.grammar
        module.fp_set_undefined(Undefined)
        self.n_callbacks = module.fp_n_python_callbacks
        for r in self.grammar.primitives(tier=3):
            getattr(module, f"fp_set_callback_{r.name}")(self._callback(r))
        self._check_layout()

    def _callback(self, r):
        mod = self.module

        def cb(*args):
            return r.ret.to_cpp(r.fn(*[t.from_cpp(a) for t, a in zip(r.args, args)]), mod)
        return cb

    def _check_layout(self):
        """Python's view of the grammar must be exactly the compiled one."""
        g = self.grammar
        if list(self.module.nonterminals()) != list(range(len(g.types()))):
            raise RuntimeError(f"internal: nonterminal order mismatch {list(self.module.nonterminals())}")
        compiled = sorted(self.module.rule_formats())
        expected = sorted((g.nt(r.ret), r.fmt) for r in g.rules)
        if compiled != expected:
            raise RuntimeError(f"internal: compiled rules {compiled} != spec {expected}")

    # ---- data ----------------------------------------------------------------------------
    def _convert_data(self, data):
        g, mod, chk = self.grammar, self.module, self.spec.likelihood.check_datum
        out = []
        for i, d in enumerate(data):
            if not isinstance(d, Datum):
                raise TypeError(f"data[{i}] is a {type(d).__name__}; use fleetpy.Datum")
            try:
                if chk is not None:
                    chk(d, g.output)
                out.append((g.input.to_cpp(d.input, mod), g.output.to_cpp(d.output, mod),
                            float(d.reliability), float(d.count)))
            except (TypeError, ValueError, OverflowError) as e:
                raise type(e)(f"data[{i}]: {e}") from None
        return out

    # ---- inference -------------------------------------------------------------------------
    def run(self, data, algo: Sampler = None, *, control: Control, top: int = 25) -> RunResult:
        from .inference import MCMCChain
        algo = MCMCChain() if algo is None else algo
        if not isinstance(algo, Sampler):
            raise TypeError("algo must be fleetpy.MCMCChain(), ChainPool(n) or ParallelTempering(n, T)")
        if not isinstance(control, Control):
            raise TypeError("control must be a fleetpy.Control")
        if algo.algo == 0 and control.threads != 1:
            raise ValueError("MCMCChain runs on one thread (Fleet asserts this); use ChainPool for threads > 1")
        if self.n_callbacks and control.threads != 1:
            raise ValueError("this model has Python-callback (tier 3) primitives, which require threads=1")
        if top < 1:
            raise ValueError("top must be >= 1")
        cdata = self._convert_data(data)
        breakout = self.spec.likelihood.nonpositive
        top_h, samples, pcalls, secs, interrupted, acc = self.module.run(
            cdata, algo.algo, algo.chains, algo.max_temp, control.steps, control.time_ms,
            control.threads, control.restart, control.seed, top, control.max_nodes, breakout)
        # Bit-for-bit reproducible only with a seed, one thread, a step budget (not a time
        # budget), and no swapper thread (ParallelTempering swaps on a wall-clock timer).
        reproducible = (control.seed != 0 and control.threads == 1 and control.time_ms == 0
                        and not (algo.algo == 2 and algo.chains > 1))
        stats = RunStats(samples, pcalls, secs, interrupted, list(acc), control.seed, control.threads,
                         reproducible, breakout, repr(control))
        return RunResult([Hypothesis(h, self) for h in top_h], stats)

    def trace(self, data, *, steps, seed, max_nodes=64, burn=0, raw=True):
        """Every state of one seeded MCMCChain: (distinct states, sequence of their indices).
        States are in the form of ``state()``. See fleetpy_runtime.h for raw=True/False."""
        return self.module.trace(self._convert_data(data), steps, burn, seed, max_nodes,
                                 self.spec.likelihood.nonpositive, raw)

    # ---- hypotheses from text -----------------------------------------------------------------
    def parse(self, s) -> Hypothesis:
        """A hypothesis from a printed program such as ``λx.and(blue(x),bigger(x,x))``; for a
        Lexicon, from a dict {key: printed program}. Needs canonical formats (see
        parsing.printed_to_parseable); deserialize() works for any formats."""
        if self.spec.is_lexicon:
            if not isinstance(s, dict):
                raise TypeError("parsing a Lexicon takes a dict {key: program}")
            return self._from_factors({k: parsing.printed_to_parseable(self.grammar, v) for k, v in s.items()})
        return self.deserialize(parsing.printed_to_parseable(self.grammar, s))

    def deserialize(self, s: str) -> Hypothesis:
        """Inverse of Hypothesis.serialize."""
        if self.spec.is_lexicon:
            return self._from_factors(dict(json.loads(s)))
        parsing.check_parseable(self.grammar, s)
        return Hypothesis(self.module.from_parseable(s), self)

    def from_state(self, state: str) -> Hypothesis:
        """Inverse of the runtime's state string (used by trace)."""
        if not self.spec.is_lexicon:
            return self.deserialize(state)
        kt = self.spec.lexicon.key_type
        fs = {}
        for part in state.split("\x1e"):
            k, _, p = part.partition("\x1f")
            fs[k if kt.category == "str" else int(k)] = p
        return self._from_factors(fs)

    def _from_factors(self, fs: dict) -> Hypothesis:
        keys = sorted(fs)
        if keys != self.spec.lexicon.keys:
            raise ValueError(f"a lexicon needs exactly the keys {self.spec.lexicon.keys}, got {keys}")
        for p in fs.values():
            parsing.check_parseable(self.grammar, p)
        return Hypothesis(self.module.from_factors([(k, fs[k]) for k in keys]), self)

    def sample(self, seed: int = 0) -> Hypothesis:
        """One draw from the prior (seed=0: continue the current RNG stream)."""
        return Hypothesis(self.module.sample(seed), self)

    def log_probability(self, h: Hypothesis) -> float:
        """Fleet's grammar log probability of h's tree(s): the PCFG prior, without MAX_NODES."""
        return self.module.log_probability(h._h)

    # ---- eject -------------------------------------------------------------------------------
    def eject(self, directory, data, algo: Sampler = None) -> Path:
        """Write a standalone Fleet model directory (Main.cpp + Makefile) with ``data`` embedded."""
        algo = algo or ParallelTempering(8, 10.0)
        d = Path(directory)
        d.mkdir(parents=True, exist_ok=True)
        self._convert_data(data)   # validate
        (d / "Main.cpp").write_text(codegen.eject_source(self.spec, data, algo))
        (d / "Makefile").write_text(
            f"# Generated by fleetpy\nFLEET_ROOT={fleet_root()}/\n\ninclude $(FLEET_ROOT)/Fleet.mk\n\n"
            f"all:\n\tg++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)\n"
            f"debug:\n\tg++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)\n")
        return d


def build(H, mode: str = None) -> Model:
    """Check, generate, compile (cached) and load the model declared by class ``H``.

    mode: "release" (default), "debug", or "checked" (ASan+UBSan; see check.run_checked).
    The FLEETPY_MODE environment variable sets the default. Every build that contains
    Python compiled to C++ differential-tests it (``model.info["differential"]``).
    """
    spec = specify(H)
    mod, info = build_module(codegen.module_source(spec), spec.json(), mode or os.environ.get("FLEETPY_MODE", "release"))
    m = Model(spec, mod, info)
    if codegen.uses_transpiled_code(spec):
        from .check import differential
        info["differential"] = differential(m)
    return m
