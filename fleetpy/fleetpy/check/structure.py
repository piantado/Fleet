"""
Structural checks on prior samples: text round trips, and the prior against an
independent computation.
"""

from __future__ import annotations

import json
import math

from .. import parsing
from .differential import CheckFailed


def roundtrip(model, n: int = 500, seed: int = 1) -> dict:
    """deserialize(serialize(h)) and parse(string(h)) must reproduce h exactly."""
    model.module.sample(seed)   # seed the stream
    bad = []
    for _ in range(n):
        h = model.sample()
        s = h.serialize()
        h2 = model.deserialize(s)
        if h2.serialize() != s or h2 != h:
            bad.append(("deserialize", s, h2.serialize()))
        if model.grammar.canonical:
            if model.spec.is_lexicon:
                h3 = model.parse(h.factors)
                ok = h3 == h and h3.factors == h.factors
            else:
                h3 = model.parse(h.string())
                ok = h3 == h and h3.string() == h.string()
            if not ok:
                bad.append(("parse", h.string(), h3.string()))
    if bad:
        raise CheckFailed("round trip failed:\n" + "\n".join(f"  {k}: {a} -> {b}" for k, a, b in bad[:8]))
    return {"samples": n, "failures": 0}


def python_log_prior(model, h) -> float:
    """The PCFG log probability computed independently of Fleet (summed over the factors of a
    Lexicon): sum over nodes of log(p_rule / Z_type), rules identified by (type, format)."""
    g = model.grammar
    Z, p = {}, {}
    for r in g.rules:
        nt = g.nt(r.ret)
        Z[nt] = Z.get(nt, 0.0) + r.p
        p[(nt, r.fmt)] = r.p
    s = h.serialize()
    programs = [q for _, q in json.loads(s)] if model.spec.is_lexicon else [s]
    return math.fsum(math.log(p[node]) - math.log(Z[node[0]])
                     for q in programs for node in parsing.parseable_rules(q))


def prior(model, n: int = 500, seed: int = 2, tol: float = 1e-9) -> dict:
    """Fleet's grammar log probability vs python_log_prior."""
    model.module.sample(seed)
    worst = 0.0
    for _ in range(n):
        h = model.sample()
        a, b = model.log_probability(h), python_log_prior(model, h)
        err = abs(a - b) / max(1.0, abs(b))
        worst = max(worst, err)
        if not err <= tol:
            raise CheckFailed(f"prior mismatch on {h}: fleet={a!r} python={b!r}")
    return {"samples": n, "max_rel_error": worst}
