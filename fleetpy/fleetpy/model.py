"""
The compiled model: the Python face of one generated nanobind module.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import codegen
from . import ftypes as T
from .build import build_module
from .grammar import Builtin, Primitive
from .hypothesis import ChainPool, Control, DeterministicHypothesis, MCMCChain, ParallelTempering


class Undefined(Exception):
    """Raise inside a primitive (any tier) to make the program's output undefined on this input.

    Compiled code maps it to Fleet's VMSRuntimeError, which the VM catches.
    """


@dataclass(frozen=True)
class Datum:
    """Mirrors Fleet's defaultdatum_t."""
    input: object
    output: object
    reliability: float = math.nan
    count: float = 1.0


class Hypothesis:
    """A handle to a C++ hypothesis, with Python-typed accessors."""

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
        """Exact and unambiguous for any formats.

        Single hypotheses: Fleet's parseable form (nt:format;...). Lexicons: JSON
        [[key, parseable], ...] in key order."""
        if self._model.H._kind == "lexicon":
            import json
            return json.dumps([[k, p] for k, p in self._h.factors()])
        return self._h.parseable()

    @property
    def factors(self) -> dict:
        """Lexicon only: {key: printed program of that factor}."""
        if self._model.H._kind != "lexicon":
            raise AttributeError("factors is only defined for a Lexicon")
        return {k: s for k, s in self._h.factor_strings()}

    def call(self, x, *, default=Undefined):
        """Deterministic: run the program on ``x``; raises fleetpy.Undefined on a runtime error
        unless default= is given. Stochastic: return {output: log mass} over all execution
        paths -- Fleet's unnormalized DiscreteDistribution (see distribution())."""
        m = self._model
        g = m.grammar
        xc = g.input.to_cpp(x, m.module)
        if m.H.stochastic:
            return {g.output.from_cpp(o): lp for o, lp in self._h.call(xc)}
        ok, out = self._h.call(xc)
        if ok:
            return g.output.from_cpp(out)
        if default is Undefined:
            raise Undefined(f"{self.string()} is undefined on {x!r}")
        return default

    def distribution(self, x, *, normalize: bool = True) -> dict:
        """Stochastic only: {output: probability}, normalized by the completed mass if asked."""
        if not self._model.H.stochastic:
            raise TypeError("distribution() is for stochastic hypotheses; use call()")
        d = self.call(x)
        if not d:
            return {}
        mx = max(d.values())
        z = mx + math.log(math.fsum(math.exp(v - mx) for v in d.values())) if normalize else 0.0
        return {k: math.exp(v - z) for k, v in d.items()}

    def logprob(self, x, y) -> float:
        """Stochastic only: log P(output = y | x), normalized (exactly the value fleetpy's
        likelihoods and @fl.likelihood's self.logprob use)."""
        m = self._model
        if not m.H.stochastic:
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
    def __init__(self, H, module, info):
        self.H = H
        self.grammar = H.grammar
        self.module = module
        self.info = info
        module.fp_set_undefined(Undefined)
        self._install_callbacks()
        self._check_layout()

    # ---- consistency checks between Python's view and the compiled grammar -------
    def _check_layout(self):
        g = self.grammar
        nts = list(self.module.nonterminals())
        if nts != list(range(len(g.all_types()))):
            raise RuntimeError(f"internal: nonterminal order mismatch {nts}")
        compiled = sorted(self.module.rule_formats())
        expected = sorted((self._nt(g.rule_signature(r)[0]), r.fmt) for r in g.rules)
        if compiled != expected:
            raise RuntimeError(f"internal: compiled rules {compiled} != spec {expected}")

    def _nt(self, t):
        keys = [x.key for x in self.grammar.all_types()]
        return keys.index(t.key)

    def _install_callbacks(self):
        g, mod = self.grammar, self.module
        for r in g.rules:
            if isinstance(r, Primitive) and r.tier == 3:
                def cb(*args, r=r):
                    py = [t.from_cpp(a) for t, a in zip(r.argtypes, args)]
                    return r.rettype.to_cpp(r.fn(*py), mod)
                getattr(mod, f"fp_set_callback_{r.name}")(cb)
        self.n_callbacks = mod.fp_n_python_callbacks

    # ---- data ------------------------------------------------------------------
    def _convert_data(self, data):
        g, mod = self.grammar, self.module
        chk = self.H.likelihood.check_datum
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

    # ---- inference ---------------------------------------------------------------
    def run(self, data, algo=None, *, control: Control, top: int = 25) -> RunResult:
        if algo is None:
            algo = MCMCChain()
        if not isinstance(algo, (MCMCChain, ChainPool, ParallelTempering)):
            raise TypeError("algo must be fleetpy.MCMCChain(), ChainPool(n) or ParallelTempering(n, T)")
        if not isinstance(control, Control):
            raise TypeError("control must be a fleetpy.Control")
        if isinstance(algo, MCMCChain) and control.threads != 1:
            raise ValueError("MCMCChain runs on one thread (Fleet asserts this); use ChainPool for threads > 1")
        if self.n_callbacks and control.threads != 1:
            raise ValueError("this model has Python-callback (tier 3) primitives, which require threads=1")
        if top < 1:
            raise ValueError("top must be >= 1")
        cdata = self._convert_data(data)
        breakout = self.H.likelihood.nonpositive
        top_h, samples, pcalls, secs, interrupted, acc = self.module.run(
            cdata, algo.algo, algo.chains, algo.max_temp, control.steps, control.time_ms,
            control.threads, control.restart, control.seed, top, control.max_nodes, breakout)
        # Bit-for-bit reproducible only with a seed, one thread, a step budget (not a time
        # budget), and no swapper thread (ParallelTempering swaps on a wall-clock timer).
        reproducible = (control.seed != 0 and control.threads == 1 and control.time_ms == 0
                        and not (isinstance(algo, ParallelTempering) and algo.chains > 1))
        stats = RunStats(samples, pcalls, secs, interrupted, list(acc), control.seed, control.threads,
                         reproducible, breakout, repr(control))
        return RunResult([Hypothesis(h, self) for h in top_h], stats)

    # ---- hypotheses from strings -------------------------------------------------------
    def deserialize(self, s: str) -> Hypothesis:
        """Inverse of Hypothesis.serialize."""
        if self.H._kind == "lexicon":
            import json
            pairs = json.loads(s)
            return self._from_factors({k: p for k, p in pairs})
        return Hypothesis(self.module.from_parseable(s), self)

    def _from_factors(self, fs: dict) -> Hypothesis:
        keys = sorted(fs)
        if keys != self.H.keys:
            raise ValueError(f"a lexicon needs exactly the keys {self.H.keys}, got {keys}")
        for k, p in fs.items():
            self._check_parseable(p)
        return Hypothesis(self.module.from_factors([(k, fs[k]) for k in keys]), self)

    def _check_parseable(self, p: str):
        """Validate a parseable string in Python (Fleet's from_parseable asserts, which is
        compiled out under NDEBUG)."""
        g = self.grammar
        toks = p.split(";")
        pos = 0

        def node(want):
            nonlocal pos
            if pos >= len(toks):
                raise ValueError(f"truncated program {p!r}")
            nt, _, fmt = toks[pos].partition(":")
            pos += 1
            r = self._rule(fmt, want)
            if str(self._nt(want)) != nt:
                raise ValueError(f"{fmt!r}: nonterminal {nt} does not match type {want.key}")
            for a in g.rule_signature(r)[1]:
                node(a)
        node(g.output)
        if pos != len(toks):
            raise ValueError(f"trailing input in program {p!r}")

    def _from_state(self, state: str) -> Hypothesis:
        """Inverse of the C++ fp_state (used by the exact-posterior check)."""
        if self.H._kind == "lexicon":
            fs = {}
            for part in state.split("\x1e"):
                k, _, p = part.partition("\x1f")
                fs[int(k) if self.H.key_type.key != "str" else k] = p
            return self._from_factors(fs)
        return Hypothesis(self.module.from_parseable(state), self)

    def _rule(self, fmt, want):
        rs = [r for r in self.grammar.rules
              if r.fmt == fmt and self.grammar.rule_signature(r)[0].key == want.key]
        if not rs:
            others = [self.grammar.rule_signature(r)[0].key for r in self.grammar.rules if r.fmt == fmt]
            if others:
                raise TypeError(f"{fmt} returns {others[0]} but {want.key} is needed here")
            raise ValueError(f"no rule with format {fmt!r}")
        return rs[0]  # (type, format) is unique: checked at build time

    def parse(self, s) -> Hypothesis:
        """Parse a printed program such as ``λx.and(blue(x),bigger(x,x))``; for a Lexicon,
        a dict {key: printed program}.

        Implemented in Python with exact rule matching and type checking, then handed to
        Fleet's from_parseable. Requires every rule format to be canonical (a head token,
        optionally followed by (%s,...) or ()); otherwise use deserialize().
        """
        g = self.grammar
        if not getattr(g, "parse_ok", True):
            raise ValueError(f"parse() needs canonical formats; these are not: {g.noncanonical}. "
                             f"Use serialize()/deserialize() instead.")
        if self.H._kind == "lexicon":
            if not isinstance(s, dict):
                raise TypeError("parsing a Lexicon takes a dict {key: program}")
            return self._from_factors({k: self._to_parseable(_parse_tree(v), g.output) for k, v in s.items()})
        return self.deserialize(self._to_parseable(_parse_tree(s), g.output))

    def _to_parseable(self, tree, want: T.FType) -> str:
        g = self.grammar
        name, args = tree
        if args is None:
            fmt = name
        elif not args:
            fmt = name + "()"
        else:
            fmt = codegen_default(name, len(args))
        r = self._rule(fmt, want)
        ret, argtypes = g.rule_signature(r)
        if len(argtypes) != len(args or []):
            raise ValueError(f"{fmt}: wrong number of arguments")
        parts = [f"{self._nt(ret)}:{fmt}"]
        for sub, at in zip(args or [], argtypes):
            parts.append(self._to_parseable(sub, at))
        return ";".join(parts)

    def _visit_counts(self, data, *, max_nodes, steps, seed, burn=None, return_sequence=False):
        """Visit counts {serialized program: count} of one chain (see check.posterior)."""
        burn = steps // 10 if burn is None else burn
        names, seq = self.module.trace(self._convert_data(data), steps, burn, seed, max_nodes,
                                       self.H.likelihood.nonpositive, False)
        n = len(seq)
        if n == 0:
            raise RuntimeError("trace recorded no finite-posterior states")
        counts = {}
        for i in seq:
            counts[i] = counts.get(i, 0) + 1
        named = {names[i]: c for i, c in counts.items()}
        if return_sequence:
            return named, n, seq, names
        return named, n

    def sample(self, seed: int = 0) -> Hypothesis:
        """One draw from the prior (seed=0: continue the current RNG stream)."""
        return Hypothesis(self.module.sample(seed), self)

    def log_probability(self, h: Hypothesis) -> float:
        """Fleet's grammar log probability of h's tree (the PCFG prior, without the MAX_NODES cutoff)."""
        return self.module.log_probability(h._h)

    # ---- eject ------------------------------------------------------------------------
    def eject(self, directory, data, algo=None) -> Path:
        """Write a standalone Fleet model directory (Main.cpp + Makefile) with ``data`` embedded."""
        from .build import fleet_root
        algo = algo or ParallelTempering(8, 10.0)
        d = Path(directory)
        d.mkdir(parents=True, exist_ok=True)
        self._convert_data(data)  # validate
        (d / "Main.cpp").write_text(codegen.eject_main_source(self.H, data, algo, None))
        root = fleet_root()
        (d / "Makefile").write_text(
            f"# Generated by fleetpy\nFLEET_ROOT={root}/\n\ninclude $(FLEET_ROOT)/Fleet.mk\n\n"
            f"all:\n\tg++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)\n"
            f"debug:\n\tg++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)\n")
        return d


def codegen_default(name, arity):
    from .grammar import default_format
    return default_format(name, arity)


_TOK = re.compile(r"\s*([^(),\s]+|\(|\)|,)")


def _parse_tree(s: str):
    """name | name(arg,...) -> (name, None | [subtrees]). Strict: rejects anything else."""
    s = s.strip()
    for prefix in ("λx.", "lambda x."):
        if s.startswith(prefix):
            s = s[len(prefix):]
    toks, pos = [], 0
    while pos < len(s):
        m = _TOK.match(s, pos)
        if not m or m.end() == pos:
            raise ValueError(f"cannot parse {s!r} at position {pos}: {s[pos:pos+20]!r}")
        toks.append(m.group(1))
        pos = m.end()
        while pos < len(s) and s[pos].isspace():
            pos += 1
    i = 0

    def node():
        nonlocal i
        if i >= len(toks) or toks[i] in ("(", ")", ","):
            raise ValueError(f"expected a name in {s!r}")
        name = toks[i]
        i += 1
        if i < len(toks) and toks[i] == "(":
            i += 1
            if i < len(toks) and toks[i] == ")":   # f()
                i += 1
                return (name, [])
            args = [node()]
            while i < len(toks) and toks[i] == ",":
                i += 1
                args.append(node())
            if i >= len(toks) or toks[i] != ")":
                raise ValueError(f"expected ')' in {s!r}")
            i += 1
            return (name, args)
        return (name, None)
    t = node()
    if i != len(toks):
        raise ValueError(f"trailing input in {s!r}: {toks[i:]}")
    return t


def build(H, mode: str = None) -> Model:
    """Generate, compile (cached) and load a model for hypothesis class ``H``.

    mode: "release" (default), "debug", or "checked" (ASan+UBSan; see check.run_checked).
    The FLEETPY_MODE environment variable sets the default.
    """
    import os
    mode = mode or os.environ.get("FLEETPY_MODE", "release")
    from .hypothesis import _HypothesisBase
    if not (isinstance(H, type) and issubclass(H, _HypothesisBase) and "_kind" not in H.__dict__):
        raise TypeError("build() expects a model class: a subclass of fl.DeterministicHypothesis, "
                        "fl.StochasticHypothesis or fl.Lexicon")
    g = H.grammar
    H.finalize()   # may add the lexicon's key terminals, so it runs before the grammar check
    g.check()
    from . import transpile
    for r in g.rules:
        if isinstance(r, Primitive) and r.tier == 2:
            r.transpiled = transpile.transpile_primitive(r)
    src = codegen.python_module_source(H, "FLEETPY_MODULE_NAME")
    mod, info = build_module(src, codegen.spec_json(H), mode)
    m = Model(H, mod, info)
    if any(isinstance(r, Primitive) and r.tier == 2 for r in g.rules) or hasattr(H.likelihood, "pyfn"):
        from .check import differential
        info["differential"] = differential(m)
    return m
