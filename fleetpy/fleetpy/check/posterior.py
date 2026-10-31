"""
The exact-posterior test: MCMC visit frequencies against the exact posterior over a finite
program space.
"""

from __future__ import annotations

import itertools
import math

from .differential import CheckFailed


def enumerate_programs(model, max_nodes: int):
    """Every state with at most max_nodes nodes per program, as runtime state strings: one
    parseable program, or for a Lexicon every combination of factor programs."""
    progs = _enumerate_trees(model.grammar, max_nodes)
    if not model.spec.is_lexicon:
        return progs
    keys = model.spec.lexicon.keys
    n = len(progs) ** len(keys)
    if n > 2_000_000:
        raise ValueError(f"the lexicon space has {n} states; use a smaller max_nodes")
    return ["\x1e".join(f"{k}\x1f{p}" for k, p in zip(keys, combo))
            for combo in itertools.product(progs, repeat=len(keys))]


def _enumerate_trees(g, max_nodes: int):
    """All complete programs of the output type with at most max_nodes nodes (parseable)."""
    by_nt = {}
    for r in g.rules:
        by_nt.setdefault(g.nt(r.ret), []).append((r.fmt, [g.nt(a) for a in r.args]))
    memo = {}

    def trees(nt, budget):
        """[(parseable, size)] for nonterminal nt with size <= budget."""
        if (nt, budget) not in memo:
            out = []
            if budget >= 1:
                for fmt, kids in by_nt.get(nt, []):
                    for parts in children(kids, budget - 1):
                        out.append((";".join([f"{nt}:{fmt}"] + [p for p, _ in parts]), 1 + sum(z for _, z in parts)))
            memo[(nt, budget)] = out
        return memo[(nt, budget)]

    def children(kids, budget):
        if not kids:
            yield []
            return
        for s, z in trees(kids[0], budget - (len(kids) - 1)):
            for tail in children(kids[1:], budget - z):
                yield [(s, z)] + tail
    return [s for s, _ in trees(g.nt(g.output), max_nodes)]


def exact_posterior(model, data, progs=None, max_nodes=None) -> dict:
    """The exact normalized posterior over an enumerated program space, computed in C++."""
    if progs is None:
        progs = enumerate_programs(model, max_nodes)
    cdata = model._convert_data(data)
    logp = {s: model.from_state(s)._h.compute_posterior(cdata)[2] for s in progs}
    finite = {s: v for s, v in logp.items() if v > -math.inf}
    if not finite:
        raise CheckFailed("every enumerated program has zero posterior")
    mx = max(finite.values())
    w = {s: math.exp(v - mx) for s, v in finite.items()}
    Z = math.fsum(w.values())
    return {s: v / Z for s, v in w.items()}


def posterior(model, data, *, max_nodes: int, steps: int = 400_000, seed: int = 7,
              alpha: float = 1e-4, min_mass: float = 0.005, exact=None) -> dict:
    """Compare one MCMC chain's visit frequencies with the exact posterior.

    The chain runs with Fleet's MAX_NODES = max_nodes, so the prior is -inf outside the
    enumerated space and the chain's target is exactly the enumerated posterior. Programs
    that are different trees but compute the same function stay distinct (they are distinct
    sampler states).

    Statistic: for every program with exact mass >= min_mass (plus one pooled "rest" bin),
    z = (freq - p) / se, with se from batch means of that bin's indicator, so each bin
    accounts for its own autocorrelation. Under the null, sum z^2 is approximately chi-square
    with (bins - 1) degrees of freedom (the frequencies sum to 1). Also reports the total
    variation distance. ``exact`` may be given to test a different target (to check power).
    """
    progs = enumerate_programs(model, max_nodes)
    if exact is None:
        exact = exact_posterior(model, data, progs)
    names, seq = model.trace(data, steps=steps, burn=steps // 10, seed=seed, max_nodes=max_nodes, raw=False)
    n = len(seq)
    if n == 0:
        raise CheckFailed("the chain recorded no finite-posterior states")
    unknown = set(names) - set(progs)
    if unknown:
        raise CheckFailed(f"sampler visited programs outside the enumerated space: {list(unknown)[:3]}")
    big = [s for s, p in exact.items() if p >= min_mass]
    bins = big + ["<rest>"]
    p_bin = {s: exact[s] for s in big}
    p_bin["<rest>"] = max(0.0, 1.0 - math.fsum(p_bin.values()))
    bigset = set(big)
    bin_of = [nm if nm in bigset else "<rest>" for nm in names]
    bsz = max(1, int(n ** 0.5))
    nbatch = n // bsz
    per_batch = {b: [0] * nbatch for b in bins}
    for k in range(nbatch * bsz):
        per_batch[bin_of[seq[k]]][k // bsz] += 1
    chi2, df, worst = 0.0, -1, (0.0, None)
    for b in bins:
        pb = p_bin[b]
        if b == "<rest>" and pb < min_mass:
            continue
        means = [c / bsz for c in per_batch[b]]
        mu = sum(means) / nbatch
        se = math.sqrt(sum((m - mu) ** 2 for m in means) / max(1, nbatch - 1) / nbatch)
        if se == 0:   # an unvisited (or always visited) bin: the binomial se at the target
            se = math.sqrt(pb * (1 - pb) / nbatch) if pb > 0 else 1.0
        z = (mu - pb) / se
        chi2 += z * z
        df += 1
        if abs(z) > abs(worst[0]):
            worst = (z, b)
    counts = [0] * len(names)
    for i in seq:
        counts[i] += 1
    freq = {nm: c / n for nm, c in zip(names, counts)}
    rep = {"space": len(progs), "bins": len(bins), "samples": n, "chi2": chi2, "df": df,
           "p_value": chi2_sf(chi2, max(df, 1)),
           "total_variation": 0.5 * math.fsum(abs(freq.get(s, 0.0) - p) for s, p in exact.items()),
           "worst_z": worst[0], "worst_program": worst[1]}
    if rep["p_value"] < alpha:
        raise CheckFailed(f"MCMC frequencies differ from the exact posterior: {rep}")
    return rep


def chi2_sf(x, k):
    """Survival function of chi-square with k dof (regularized upper incomplete gamma)."""
    a, x2 = k / 2.0, x / 2.0
    if x2 <= 0:
        return 1.0
    if x2 < a + 1:   # series for P; return 1 - P
        term = s = 1.0 / a
        ap = a
        for _ in range(10000):
            ap += 1
            term *= x2 / ap
            s += term
            if abs(term) < abs(s) * 1e-15:
                break
        return max(0.0, 1.0 - s * math.exp(-x2 + a * math.log(x2) - math.lgamma(a)))
    b = x2 + 1 - a   # continued fraction for Q (modified Lentz)
    c = 1.0 / 1e-300
    d = 1.0 / b
    h = d
    for i in range(1, 10000):
        an = -i * (i - a)
        b += 2
        d = an * d + b
        d = 1e-300 if abs(d) < 1e-300 else d
        c = b + an / c
        c = 1e-300 if abs(c) < 1e-300 else c
        d = 1.0 / d
        de = d * c
        h *= de
        if abs(de - 1) < 1e-15:
            break
    return math.exp(-x2 + a * math.log(x2) - math.lgamma(a)) * h
