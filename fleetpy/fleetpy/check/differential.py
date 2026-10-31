"""
Differential testing: every piece of Python compiled to C++ is run both ways, on generated
inputs, and must agree exactly (floats bit for bit, "undefined" included).

  primitives    compiled ``fp_prim_<name>`` vs the int64 reference of its source
  likelihood    the compiled likelihood alone (no prior) vs the reference of its source,
                with ``self`` calling the compiled hypothesis
  prior         compiled compute_prior vs the reference of its source
"""

from __future__ import annotations

import math
import random
import struct

from .reference import References

UNDEF = object()
_PYTHON_RAISES = (ZeroDivisionError, IndexError, ValueError, OverflowError)   # + fleetpy.Undefined


class CheckFailed(AssertionError):
    pass


def same(a, b) -> bool:
    """Exact equality; floats compared bit for bit, except that any NaN equals any NaN."""
    if isinstance(a, float) and isinstance(b, float):
        return (math.isnan(a) and math.isnan(b)) or struct.pack("<d", a) == struct.pack("<d", b)
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        return type(a) is type(b) and len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if hasattr(a, "__dataclass_fields__") and type(a) is type(b):
        return all(same(getattr(a, f), getattr(b, f)) for f in a.__dataclass_fields__)
    return type(a) is type(b) and a == b


def _show(v):
    return "undefined" if v is UNDEF else repr(v)


def _run(f, *args, on_error=UNDEF):
    """Call f with Python semantics; whatever Python raises means 'undefined'."""
    from ..model import Undefined
    try:
        return f(*args)
    except (Undefined, *_PYTHON_RAISES):
        return on_error


class SelfShim:
    """``self`` for running a likelihood's or prior's Python source against a compiled hypothesis."""
    _NODEFAULT = object()

    def __init__(self, h):
        self._h = h

    def call(self, x, default=_NODEFAULT):
        return self._h.call(x) if default is self._NODEFAULT else self._h.call(x, default=default)

    def logprob(self, x, y):
        return self._h.logprob(x, y)   # the same C++ computation the compiled code calls

    def grammar_prior(self):
        return self._h._h.grammar_prior()

    def recursion_count(self):
        return self._h._h.recursion_count()

    def nodes(self):
        return self._h.nodes


def _fail(what, n, cases):
    ex = "\n".join(f"    {c}" for c in cases[:8])
    raise CheckFailed(f"{what}: compiled C++ disagrees with the Python source on {len(cases)}/{n} cases:\n{ex}")


def primitive(model, r, refs, plain, rng, n):
    mod = model.module
    compiled = getattr(mod, f"fp_prim_{r.name}")
    bad, undefined, limited = [], 0, 0
    for i in range(n):
        args = [t.sample(rng, i % 3 == 0) for t in r.args]

        def reference(f):
            out = _run(f, *args)
            if out is None:                 # fell off the end of the function
                return UNDEF
            if out is not UNDEF:
                try:
                    r.ret.to_cpp(out, mod)  # the range check compiled code applies to the result
                except OverflowError:
                    return UNDEF
            return out
        want, unbounded = reference(refs.primitives[r.name]), reference(plain.primitives[r.name])
        ok, got = compiled(*[t.to_cpp(a, mod) for t, a in zip(r.args, args)])
        got = r.ret.from_cpp(got) if ok else UNDEF
        undefined += want is UNDEF
        limited += (want is UNDEF) != (unbounded is UNDEF) or (want is not UNDEF and not same(want, unbounded))
        if (want is UNDEF) != (got is UNDEF) or (want is not UNDEF and not same(want, got)):
            bad.append(f"{r.name}{tuple(args)!r}: python={_show(want)} c++={_show(got)}")
    if bad:
        _fail(f"tier-2 primitive {r.name}", n, bad)
    return {"cases": n, "undefined": undefined, "mismatches": 0, "int64_limited": limited}


def _score(f, *args) -> float:
    """A Python likelihood or prior as compiled code sees it: undefined (an exception, or
    falling off the end) is -inf."""
    v = _run(f, *args, on_error=None)
    return -math.inf if v is None else float(v)


def _random_datum(model, rng, edge):
    from ..model import Datum
    g = model.grammar
    return Datum(g.input.sample(rng, edge), g.output.sample(rng, edge),
                 rng.choice([0.0, 0.5, 0.9, 1.0, rng.random()]), 1.0)


def likelihood(model, refs, rng, n):
    lk = model.spec.likelihood
    f = refs.method(lk.pyfn)
    bad = []
    for i in range(n):
        h = model.sample()
        if lk.scope == "total":
            data = [_random_datum(model, rng, i % 3 == 0) for _ in range(rng.randint(0, 3))]
            want = _score(f, SelfShim(h), data)
            got = h._h.total_likelihood(model._convert_data(data))
        else:
            data = _random_datum(model, rng, i % 3 == 0)
            want = _score(f, SelfShim(h), data)
            got = h._h.single_likelihood(model._convert_data([data])[0])
        if not same(want, got):
            bad.append(f"{h.string()} on {data}: python={want!r} c++={got!r}")
    if bad:
        _fail("likelihood", n, bad)
    return {"cases": n, "mismatches": 0}


def prior(model, refs, n):
    f = refs.method(model.spec.prior.pyfn)
    bad = []
    for _ in range(n):
        h = model.sample()
        want = _score(f, SelfShim(h))
        got = h._h.compute_prior()
        if not same(want, got):
            bad.append(f"{h.string()}: python={want!r} c++={got!r}")
    if bad:
        _fail("prior", n, bad)
    return {"cases": n, "mismatches": 0}


def differential(model, n: int = 600, seed: int = 12345) -> dict:
    """Compare every piece of transpiled Python in ``model`` with its source. Raises
    CheckFailed on any disagreement; returns a report."""
    spec, rng = model.spec, random.Random(seed)
    refs, plain = References(spec.grammar), References(spec.grammar, checked=False)
    report = {r.name: primitive(model, r, refs, plain, rng, n) for r in spec.grammar.primitives(tier=2)}
    if spec.likelihood.transpiled:
        report["likelihood"] = likelihood(model, refs, rng, n // 6 if spec.likelihood.scope == "total" else n // 3)
    if spec.prior is not None and spec.prior.transpiled:
        report["prior"] = prior(model, refs, n // 3)
    return report
