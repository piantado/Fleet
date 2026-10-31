"""
Correctness checks.

  differential(model)        -- tier-2 primitives and likelihoods: Python reference vs compiled C++
  roundtrip(model, n)        -- string -> parse -> string, and serialize -> deserialize, on prior samples
  prior(model, n)            -- Fleet's compute_prior vs an independent Python PCFG computation
  posterior(model, data, ...)-- MCMC visit frequencies vs the exact posterior over a finite space

Each returns a report dict and raises CheckFailed when the check fails.
"""

from __future__ import annotations

import ast
import itertools
import math
import random
import struct as _struct

from . import ftypes as T
from .grammar import Builtin, Primitive


class CheckFailed(AssertionError):
    pass


# --------------------------------------------------------------------------------------
# value generation and comparison
# --------------------------------------------------------------------------------------

def _gen(t: T.FType, rng: random.Random, edge: bool):
    if isinstance(t, T._Bool):
        return rng.choice([False, True])
    if isinstance(t, T._Int):
        if edge:
            return rng.choice([0, 1, -1, 2, -2, t.lo, t.hi, t.lo + 1, t.hi - 1, 7, -7])
        if rng.random() < 0.7:
            return rng.randint(max(t.lo, -20), min(t.hi, 20))
        return rng.randint(t.lo, t.hi)
    if isinstance(t, T._Float):
        if edge:
            return rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e-300, -1e-300, 1e300, -1e300,
                               math.inf, -math.inf, math.nan, 5e-324, 1.7976931348623157e308])
        return rng.choice([rng.gauss(0, 3), rng.uniform(-100, 100), float(rng.randint(-5, 5))])
    if isinstance(t, T._Char):
        return rng.choice("ab01xyz ~\x00")
    if isinstance(t, T.OpaqueType) and t.py is not None:
        for _ in range(100):
            v = _gen(t.py, rng, edge)
            if t.check is None or t.check(v):
                return v
        raise TypeError(f"cannot generate values for {t.cpp}: its check rejects 100 random {t.py.key}s")
    if isinstance(t, T._Str):
        pool = ["", "a", "b", "ab", "ba", "abc", "é", "日本", "á", "z" * 5]
        if edge or rng.random() < 0.5:
            return rng.choice(pool)
        return "".join(rng.choice("abcxyz") for _ in range(rng.randint(0, 6)))
    if isinstance(t, T.ListOf):
        return [_gen(t.elem, rng, edge and rng.random() < 0.5) for _ in range(rng.randint(0, 4))]
    if isinstance(t, T.TupleOf):
        return tuple(_gen(e, rng, edge) for e in t.elems)
    if isinstance(t, T.SetOf):
        return frozenset(_gen(t.elem, rng, edge) for _ in range(rng.randint(0, 4)))
    if isinstance(t, T.StructType):
        return t.pycls(*[_gen(ft, rng, edge and rng.random() < 0.5) for _, ft in t.fields])
    if isinstance(t, T.NewType):
        return _gen(t.base, rng, edge)
    raise TypeError(f"cannot generate values of {t!r}")


def _same(a, b) -> bool:
    """Exact equality; floats compared bit-for-bit except that any NaN equals any NaN."""
    if isinstance(a, float) and isinstance(b, float):
        if math.isnan(a) and math.isnan(b):
            return True
        return _struct.pack("<d", a) == _struct.pack("<d", b)
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        return type(a) is type(b) and len(a) == len(b) and all(_same(x, y) for x, y in zip(a, b))
    if hasattr(a, "__dataclass_fields__") and type(a) is type(b):
        return all(_same(getattr(a, f), getattr(b, f)) for f in a.__dataclass_fields__)
    return type(a) is type(b) and a == b


_UNDEF = object()


def _py_reference(fn, args, rettype, mod):
    """Run the Python function with Python semantics; map Python errors to 'undefined'."""
    from .model import Undefined
    try:
        r = fn(*args)
    except (Undefined, ZeroDivisionError, IndexError, ValueError, OverflowError):
        return _UNDEF
    if r is None:
        return _UNDEF  # fell off the end of the function
    try:
        rettype.to_cpp(r, mod)  # the same range check the compiled code applies when narrowing
    except OverflowError:
        return _UNDEF
    return r


# --------------------------------------------------------------------------------------
# the int64 reference: the user's own Python source, run by CPython, with every integer
# operation checked against int64 (fleetpy's documented integer semantics). This is an
# independent implementation of the spec -- it shares nothing with the C++ code path.
# --------------------------------------------------------------------------------------

_I64_LO, _I64_HI = -(1 << 63), (1 << 63) - 1
_EXACT = 1 << 53


class _Chk:
    stack = []  # per-call [iterations, cap], innermost last

    @staticmethod
    def _i(v):
        return isinstance(v, int) and not isinstance(v, bool)

    @classmethod
    def rng(cls, *args):
        for v in args:
            cls._range(v)
        for x in range(*args):        # range(..., 0) raises ValueError -> undefined, as in C++
            top = cls.stack[-1]
            top[0] += 1
            if top[0] > top[1]:
                raise OverflowError("iteration cap")
            yield x

    @classmethod
    def _range(cls, *vs):
        for v in vs:
            if cls._i(v) and not (_I64_LO <= v <= _I64_HI):
                raise OverflowError("int64 overflow")

    @classmethod
    def binop(cls, op, a, b):
        cls._range(a, b)
        if op == "/" and cls._i(a) and cls._i(b) and b != 0 and (abs(a) > _EXACT or abs(b) > _EXACT):
            raise OverflowError("int/int beyond 2^53")
        if op == "<<" and cls._i(b) and b > 4096 and a != 0:
            raise OverflowError("shift too large")   # (a << huge) cannot fit int64 unless a == 0
        r = _OPS[op](a, b)
        cls._range(r)
        return r

    @classmethod
    def neg(cls, a):
        cls._range(a)
        r = -a
        cls._range(r)
        return r

    @classmethod
    def call(cls, f, *args):
        cls._range(*args)
        r = f(*args)
        cls._range(r)
        return r


def _narrow_ok(t, v) -> bool:
    """Does v fit the declared fleetpy type t (the checks the compiled code applies)?"""
    t = t.base if isinstance(t, T.NewType) else t
    if isinstance(t, T._Int):
        return isinstance(v, int) and not isinstance(v, bool) and t.lo <= v <= t.hi
    if isinstance(t, T._Char):
        return isinstance(v, str) and len(v) == 1 and ord(v) < 128
    if isinstance(t, T.StructType):
        return all(_narrow_ok(ft, getattr(v, n)) for n, ft in t.fields)
    if isinstance(t, T.ListOf):
        return all(_narrow_ok(t.elem, x) for x in v)
    if isinstance(t, T.TupleOf):
        return all(_narrow_ok(e, x) for e, x in zip(t.elems, v))
    return True


def _mkstruct(cls, *args):
    st = cls.__fleet_type__
    for (n, ft), a in zip(st.fields, args):
        if isinstance(ft, T._Int) or (isinstance(ft, T.NewType) and isinstance(ft.base, T._Int)):
            if not _narrow_ok(ft, a):
                raise OverflowError(f"{st.name}.{n}: {a} does not fit {ft.key}")
        if isinstance(ft, T._Float) and isinstance(a, int) and not isinstance(a, bool):
            a = float(a)
    return cls(*[float(a) if isinstance(ft, T._Float) and isinstance(a, int) and not isinstance(a, bool)
                 else a for (n, ft), a in zip(st.fields, args)])


class _NoChk(_Chk):
    """Unbounded Python ints, but the same iteration caps (for the informational comparison)."""
    stack = []

    @classmethod
    def _range(cls, *vs):
        pass

    @classmethod
    def binop(cls, op, a, b):
        if op == "<<" and cls._i(b) and b > 4096 and a != 0:
            raise OverflowError("shift too large")   # keep the unbounded reference finite
        return _OPS[op](a, b)


import operator as _op
_OPS = {"+": _op.add, "-": _op.sub, "*": _op.mul, "/": _op.truediv, "//": _op.floordiv, "%": _op.mod,
        "**": _op.pow, "&": _op.and_, "|": _op.or_, "^": _op.xor, "<<": _op.lshift, ">>": _op.rshift}


class _Rewrite(ast.NodeTransformer):
    OPS = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.FloorDiv: "//",
           ast.Mod: "%", ast.Pow: "**", ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^",
           ast.LShift: "<<", ast.RShift: ">>"}

    def __init__(self, structs=()):
        super().__init__()
        self.structs = set(structs)

    def visit_BinOp(self, n):
        self.generic_visit(n)
        return ast.copy_location(ast.Call(
            func=ast.Attribute(value=ast.Name("__fp_chk", ast.Load()), attr="binop", ctx=ast.Load()),
            args=[ast.Constant(self.OPS[type(n.op)]), n.left, n.right], keywords=[]), n)

    def visit_UnaryOp(self, n):
        self.generic_visit(n)
        if isinstance(n.op, ast.USub):
            return ast.copy_location(ast.Call(
                func=ast.Attribute(value=ast.Name("__fp_chk", ast.Load()), attr="neg", ctx=ast.Load()),
                args=[n.operand], keywords=[]), n)
        return n

    def visit_AugAssign(self, n):
        self.generic_visit(n)
        tgt = ast.Name(n.target.id, ast.Load())
        val = ast.Call(func=ast.Attribute(value=ast.Name("__fp_chk", ast.Load()), attr="binop",
                                          ctx=ast.Load()),
                       args=[ast.Constant(self.OPS[type(n.op)]), tgt, n.value], keywords=[])
        return ast.copy_location(ast.Assign(targets=[ast.Name(n.target.id, ast.Store())], value=val), n)

    def visit_For(self, n):
        self.generic_visit(n)
        it = n.iter
        if isinstance(it, ast.Call) and isinstance(it.func, ast.Name) and it.func.id == "range":
            n.iter = ast.copy_location(ast.Call(
                func=ast.Attribute(value=ast.Name("__fp_chk", ast.Load()), attr="rng", ctx=ast.Load()),
                args=it.args, keywords=[]), it)
        return n

    def visit_Call(self, n):
        self.generic_visit(n)
        f = n.func
        is_math = isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "math" \
            and f.attr in ("floor", "ceil", "trunc")
        is_builtin = isinstance(f, ast.Name) and f.id in ("int", "abs")
        if isinstance(f, ast.Name) and f.id in self.structs:
            return ast.copy_location(ast.Call(func=ast.Name("__fp_mkstruct", ast.Load()),
                                              args=[f] + n.args, keywords=[]), n)
        if is_math or is_builtin:
            return ast.copy_location(ast.Call(
                func=ast.Attribute(value=ast.Name("__fp_chk", ast.Load()), attr="call", ctx=ast.Load()),
                args=[f] + n.args, keywords=[]), n)
        return n


def int64_references(grammar, checked: bool = True):
    """{name: function} -- every tier-2 primitive rewritten with int64-checked integer ops
    (checked=False: unbounded ints) and the same per-call loop iteration cap as the C++.

    Calls between tier-2 primitives resolve to the rewritten versions."""
    import functools
    import inspect
    import textwrap
    chk = _Chk if checked else _NoChk
    prims = [r for r in grammar.rules if isinstance(r, Primitive) and r.tier == 2]
    out = {}
    for r in prims:
        ns = dict(r.fn.__globals__)
        ns["__fp_chk"] = chk
        ns["__fp_mkstruct"] = _mkstruct
        structs = [k for k, v in ns.items() if isinstance(v, type) and
                   isinstance(getattr(v, "__fleet_type__", None), T.StructType)]
        src = textwrap.dedent(inspect.getsource(r.fn))
        tree = ast.parse(src)
        fdef = tree.body[0]
        fdef.decorator_list = []
        tree = ast.fix_missing_locations(_Rewrite(structs).visit(tree))
        exec(compile(tree, f"<int64 reference of {r.name}>", "exec"), ns)
        raw = ns[r.name]

        def wrapped(*a, _raw=raw, _cap=r.max_iters, _r=r):
            # argument and return narrowing, exactly where the compiled code narrows
            for t, v in zip(_r.argtypes, a):
                if not _narrow_ok(t, v):
                    raise OverflowError(f"{_r.name}: argument {v!r} does not fit {t.key}")
            chk.stack.append([0, _cap])
            try:
                out = _raw(*a)
            finally:
                chk.stack.pop()
            if out is not None and not _narrow_ok(_r.rettype, out):
                raise OverflowError(f"{_r.name}: result {out!r} does not fit {_r.rettype.key}")
            if out is not None and isinstance(_r.rettype, T._Float) and isinstance(out, int) \
                    and not isinstance(out, bool):
                out = float(out)
            return out
        out[r.name] = (ns, functools.wraps(raw)(wrapped))
    for name, (ns, _) in out.items():
        for other, (_, f) in out.items():
            ns[other] = f
    return {k: f for k, (_, f) in out.items()}


# --------------------------------------------------------------------------------------
# differential testing
# --------------------------------------------------------------------------------------

def differential(model, n: int = 600, seed: int = 12345) -> dict:
    """Compare every tier-2 primitive (and a transpiled likelihood) against its Python source."""
    rng = random.Random(seed)
    mod = model.module
    report = {}
    any2 = any(isinstance(r, Primitive) and r.tier == 2 for r in model.grammar.rules)
    refs = int64_references(model.grammar) if any2 else {}
    plains = int64_references(model.grammar, checked=False) if any2 else {}
    for r in model.grammar.rules:
        if not (isinstance(r, Primitive) and r.tier == 2):
            continue
        cfn = getattr(mod, f"fp_prim_{r.name}")
        mismatches, undefined, int64_limited = [], 0, []
        for i in range(n):
            edge = i % 3 == 0
            args = [_gen(t, rng, edge) for t in r.argtypes]
            want = _py_reference(refs[r.name], args, r.rettype, mod)   # int64 semantics
            plain = _py_reference(plains[r.name], args, r.rettype, mod)  # unbounded Python ints
            ok, got = cfn(*[t.to_cpp(a, mod) for t, a in zip(r.argtypes, args)])
            got = r.rettype.from_cpp(got) if ok else _UNDEF
            if want is _UNDEF:
                undefined += 1
            if (want is _UNDEF) != (plain is _UNDEF) or (want is not _UNDEF and not _same(want, plain)):
                int64_limited.append(args)
            if (want is _UNDEF) != (got is _UNDEF) or (want is not _UNDEF and not _same(want, got)):
                mismatches.append((args, "undefined" if want is _UNDEF else want,
                                   "undefined" if got is _UNDEF else got))
        report[r.name] = {"cases": n, "undefined": undefined, "mismatches": len(mismatches),
                          "int64_limited": len(int64_limited)}
        if mismatches:
            ex = "\n".join(f"    {r.name}{tuple(a)!r}: python={w!r} c++={g!r}" for a, w, g in mismatches[:8])
            raise CheckFailed(f"tier-2 primitive {r.name}: compiled C++ disagrees with the Python "
                              f"source on {len(mismatches)}/{n} inputs:\n{ex}")
    lk = model.H.likelihood
    if hasattr(lk, "pyfn"):
        if getattr(lk, "total", False):
            report["likelihood"] = _differential_total_likelihood(model, lk, rng, n)
        else:
            report["likelihood"] = _differential_likelihood(model, lk, rng, n)
    pp = getattr(model.H, "_pending_prior", None)
    if pp is not None:
        report["prior"] = _differential_prior(model, pp.fn, n)
    return report


def _differential_total_likelihood(model, lk, rng, n):
    from .model import Datum, Undefined
    g = model.grammar
    mism = []
    for i in range(n // 6):
        h = model.sample()
        data = [Datum(_gen(g.input, rng, i % 3 == 0), _gen(g.output, rng, i % 3 == 0),
                      rng.choice([0.0, 0.5, 0.9, 1.0]), 1.0) for _ in range(rng.randint(0, 3))]
        try:
            want = float(lk.pyfn(_SelfShim(h), data))
        except (Undefined, ZeroDivisionError, IndexError, ValueError, OverflowError):
            want = -math.inf
        got = h._h.total_likelihood(model._convert_data(data))
        if not _same(want, got):
            mism.append((h.string(), data, want, got))
    if mism:
        ex = "\n".join(f"    {s} on {d}: python={w!r} c++={g!r}" for s, d, w, g in mism[:8])
        raise CheckFailed(f"transpiled whole-dataset likelihood disagrees with its Python source:\n{ex}")
    return {"cases": n // 6, "mismatches": 0}


def _differential_prior(model, fn, n):
    from .model import Undefined
    mism = []
    for _ in range(n // 3):
        h = model.sample()
        try:
            want = float(fn(_SelfShim(h)))
        except (Undefined, ZeroDivisionError, IndexError, ValueError, OverflowError):
            want = -math.inf
        got = h._h.compute_prior()
        if not _same(want, got):
            mism.append((h.string(), want, got))
    if mism:
        ex = "\n".join(f"    {s}: python={w!r} c++={g!r}" for s, w, g in mism[:8])
        raise CheckFailed(f"transpiled prior disagrees with its Python source:\n{ex}")
    return {"cases": n // 3, "mismatches": 0}


_NODEF = object()


class _SelfShim:
    """`self` for running a tier-2 likelihood's or prior's Python source against a compiled hypothesis."""

    def __init__(self, h):
        self._h = h

    def call(self, x, default=_NODEF):
        if default is _NODEF:
            return self._h.call(x)  # raises fleetpy.Undefined on error
        return self._h.call(x, default=default)

    def grammar_prior(self):
        return self._h._h.grammar_prior()

    def recursion_count(self):
        return self._h._h.recursion_count()

    def nodes(self):
        return self._h.nodes

    def logprob(self, x, y):
        return self._h.logprob(x, y)  # the same C++ computation the transpiled code calls


def _differential_likelihood(model, lk, rng, n):
    from .model import Datum, Undefined
    g = model.grammar
    mism = []
    for i in range(n // 3):
        h = model.sample()
        d = Datum(_gen(g.input, rng, i % 3 == 0), _gen(g.output, rng, i % 3 == 0),
                  rng.choice([0.0, 0.5, 0.9, 1.0, rng.random()]), 1.0)
        try:
            want = float(lk.pyfn(_SelfShim(h), d))
        except (Undefined, ZeroDivisionError, IndexError, ValueError, OverflowError):
            want = -math.inf
        got = h._h.single_likelihood(model._convert_data([d])[0])
        if not _same(want, got):
            mism.append((h.string(), d, want, got))
    if mism:
        ex = "\n".join(f"    {s} on {d}: python={w!r} c++={g!r}" for s, d, w, g in mism[:8])
        raise CheckFailed(f"transpiled likelihood disagrees with its Python source:\n{ex}")
    return {"cases": n // 3, "mismatches": 0}


# --------------------------------------------------------------------------------------
# round trips and the prior
# --------------------------------------------------------------------------------------

def roundtrip(model, n: int = 500, seed: int = 1) -> dict:
    """string -> parse -> string and serialize -> deserialize must be identities on prior samples."""
    model.module.sample(seed)  # seed the stream
    bad = []
    lex = model.H._kind == "lexicon"
    for _ in range(n):
        h = model.sample()
        s = h.serialize()
        h2 = model.deserialize(s)
        if h2.serialize() != s or h2 != h:
            bad.append(("deserialize", s, h2.serialize()))
        if model.grammar.parse_ok:
            if lex:
                printed = {k: v for k, v in h.factors.items()}
                h3 = model.parse(printed)
                ok = h3 == h and h3.factors == printed
            else:
                h3 = model.parse(h.string())
                ok = h3 == h and h3.string() == h.string()
            if not ok:
                bad.append(("parse", h.string(), h3.string()))
    if bad:
        raise CheckFailed("round trip failed:\n" + "\n".join(f"  {k}: {a} -> {b}" for k, a, b in bad[:8]))
    return {"samples": n, "failures": 0}


def _trees(model, h):
    """Every factor's rule tree from the serialized form: lists of (nt, fmt) in preorder."""
    import json
    s = h.serialize()
    parts = [p for _, p in json.loads(s)] if model.H._kind == "lexicon" else [s]
    return [[(int(t.split(":", 1)[0]), t.split(":", 1)[1]) for t in p.split(";")] for p in parts]


def python_log_prior(model, h) -> float:
    """Independent PCFG log probability (summed over factors for a Lexicon):
    sum over nodes of log(p_rule / Z_nt), with rules identified by (nonterminal, format)."""
    g = model.grammar
    Z, p = {}, {}
    for r in g.rules:
        nt = model._nt(g.rule_signature(r)[0])
        Z[nt] = Z.get(nt, 0.0) + r.p
        p[(nt, r.fmt)] = r.p
    return math.fsum(math.log(p[(nt, fmt)]) - math.log(Z[nt])
                     for tree in _trees(model, h) for nt, fmt in tree)


def prior(model, n: int = 500, seed: int = 2, tol: float = 1e-9) -> dict:
    """Fleet's grammar log_probability vs the independent Python PCFG computation."""
    model.module.sample(seed)
    worst = 0.0
    for _ in range(n):
        h = model.sample()
        a = model.log_probability(h)
        b = python_log_prior(model, h)
        err = abs(a - b) / max(1.0, abs(b))
        worst = max(worst, err)
        if not err <= tol:
            raise CheckFailed(f"prior mismatch on {h}: fleet={a!r} python={b!r}")
    return {"samples": n, "max_rel_error": worst}


# --------------------------------------------------------------------------------------
# exact posterior test
# --------------------------------------------------------------------------------------

def enumerate_programs(model, max_nodes: int):
    """All states with at most max_nodes nodes per program, as the C++ state strings: one
    parseable program, or for a Lexicon every combination of factor programs."""
    progs = _enumerate_single(model, max_nodes)
    if model.H._kind != "lexicon":
        return progs
    keys = model.H.keys
    n = len(progs) ** len(keys)
    if n > 2_000_000:
        raise ValueError(f"the lexicon space has {n} states; use a smaller max_nodes")
    return ["\x1e".join(f"{k}\x1f{p}" for k, p in zip(keys, combo))
            for combo in itertools.product(progs, repeat=len(keys))]


def _enumerate_single(model, max_nodes: int):
    """All complete programs of the output type with at most max_nodes nodes (as serialized strings)."""
    g = model.grammar
    by_nt = {}
    for r in g.rules:
        ret, args = g.rule_signature(r)
        by_nt.setdefault(model._nt(ret), []).append((r.fmt, [model._nt(a) for a in args]))
    memo = {}

    def gen(nt, budget):
        """List of (serialized, size) for trees of nonterminal nt with size <= budget."""
        key = (nt, budget)
        if key in memo:
            return memo[key]
        out = []
        if budget >= 1:
            for fmt, kids in by_nt.get(nt, []):
                for parts in _children(kids, budget - 1):
                    s = ";".join([f"{nt}:{fmt}"] + [p for p, _ in parts])
                    out.append((s, 1 + sum(z for _, z in parts)))
        memo[key] = out
        return out

    def _children(kids, budget):
        if not kids:
            yield []
            return
        first, rest = kids[0], kids[1:]
        min_rest = len(rest)
        for s, z in gen(first, budget - min_rest):
            for tail in _children(rest, budget - z):
                yield [(s, z)] + tail
    return [s for s, _ in gen(model._nt(g.output), max_nodes)]


def posterior(model, data, *, max_nodes: int, steps: int = 400_000, seed: int = 7,
              alpha: float = 1e-4, min_mass: float = 0.005, exact=None) -> dict:
    """Compare one MCMC chain's visit frequencies with the exact posterior over a finite space.

    fleetpy sets Fleet's MAX_NODES = max_nodes for the run, so the prior is -inf outside the
    enumerated space and the chain's target is exactly the enumerated posterior. Programs
    that are different trees but compute the same function stay distinct (they are distinct
    sampler states).

    Statistic: for every program with exact mass >= min_mass (plus one pooled "rest" bin),
    z = (freq - p) / se, where se comes from batch means of that bin's indicator, so each bin
    accounts for its own autocorrelation. Under the null, sum z^2 is approximately chi-square
    with (bins - 1) degrees of freedom (the frequencies sum to 1). Also reports total variation.
    ``exact`` may be passed to test against a different target (used to check the test's power).
    """
    progs = enumerate_programs(model, max_nodes)
    if exact is None:
        exact = exact_posterior(model, data, progs)
    counts, n, seq, names = model._visit_counts(data, max_nodes=max_nodes, steps=steps, seed=seed,
                                                return_sequence=True)
    unknown = set(counts) - set(progs)
    if unknown:
        raise CheckFailed(f"sampler visited programs outside the enumerated space: {list(unknown)[:3]}")
    big = [s for s, p in exact.items() if p >= min_mass]
    bigset = set(big)
    bins = big + ["<rest>"]
    p_bin = {s: exact[s] for s in big}
    p_bin["<rest>"] = max(0.0, 1.0 - math.fsum(p_bin.values()))
    sid_to_bin = {i: (nm if nm in bigset else "<rest>") for i, nm in enumerate(names)}
    # batch means of each bin's indicator
    bsz = max(1, int(n ** 0.5))
    nbatch = n // bsz
    per_batch = {b_: [0] * nbatch for b_ in bins}
    for k in range(nbatch * bsz):
        per_batch[sid_to_bin[seq[k]]][k // bsz] += 1
    chi2, df, worst = 0.0, -1, (0.0, None)
    for b_ in bins:
        pb = p_bin[b_]
        if b_ == "<rest>" and pb < min_mass:
            continue
        means = [c / bsz for c in per_batch[b_]]
        mu = sum(means) / nbatch
        var_b = sum((m - mu) ** 2 for m in means) / max(1, nbatch - 1)
        se = math.sqrt(var_b / nbatch)
        # an unvisited bin has se=0; fall back to the binomial se at the target (conservative-free)
        if se == 0:
            se = math.sqrt(pb * (1 - pb) / nbatch) if pb > 0 else 1.0
        z = (mu - pb) / se
        chi2 += z * z
        df += 1
        if abs(z) > abs(worst[0]):
            worst = (z, b_)
    pval = _chi2_sf(chi2, max(df, 1))
    freq = {nm: c / n for nm, c in counts.items()}
    tv = 0.5 * math.fsum(abs(freq.get(s, 0.0) - p) for s, p in exact.items())
    rep = {"space": len(progs), "bins": len(bins), "samples": n, "chi2": chi2, "df": df,
           "p_value": pval, "total_variation": tv, "worst_z": worst[0], "worst_program": worst[1]}
    if pval < alpha:
        raise CheckFailed(f"MCMC frequencies differ from the exact posterior: {rep}")
    return rep


def exact_posterior(model, data, progs=None, max_nodes=None):
    """Exact normalized posterior over an enumerated program space, computed in C++."""
    if progs is None:
        progs = enumerate_programs(model, max_nodes)
    cdata = model._convert_data(data)
    logp = {}
    for s in progs:
        pr, lk, po = model._from_state(s)._h.compute_posterior(cdata)
        logp[s] = po
    finite = {s: v for s, v in logp.items() if v > -math.inf}
    if not finite:
        raise CheckFailed("every enumerated program has zero posterior")
    mx = max(finite.values())
    w = {s: math.exp(v - mx) for s, v in finite.items()}
    Z = math.fsum(w.values())
    return {s: v / Z for s, v in w.items()}


def _chi2_sf(x, k):
    """Survival function of chi-square with k dof (regularized upper incomplete gamma)."""
    a, x2 = k / 2.0, x / 2.0
    if x2 <= 0:
        return 1.0
    if x2 < a + 1:  # series for P, return 1-P
        term = s = 1.0 / a
        ap = a
        for _ in range(10000):
            ap += 1
            term *= x2 / ap
            s += term
            if abs(term) < abs(s) * 1e-15:
                break
        return max(0.0, 1.0 - s * math.exp(-x2 + a * math.log(x2) - math.lgamma(a)))
    # continued fraction for Q (Lentz)
    b = x2 + 1 - a
    cc = 1.0 / 1e-300
    d = 1.0 / b
    h = d
    for i in range(1, 10000):
        an = -i * (i - a)
        b += 2
        d = an * d + b
        d = 1e-300 if abs(d) < 1e-300 else d
        cc = b + an / cc
        cc = 1e-300 if abs(cc) < 1e-300 else cc
        d = 1.0 / d
        de = d * cc
        h *= de
        if abs(de - 1) < 1e-15:
            break
    return math.exp(-x2 + a * math.log(x2) - math.lgamma(a)) * h


# --------------------------------------------------------------------------------------
# sanitizer runs
# --------------------------------------------------------------------------------------

# Known upstream Fleet reports, suppressed narrowly (by check type and function) so that a
# checked run still reports everything else. Each entry: (ubsan suppression, explanation).
KNOWN_FLEET_UBSAN = [
    ("vptr_check:Node",
     "src/BaseNode.h:252: BaseNode's constructors call fix_child_info(), which stores "
     "static_cast<Node*>(this) while Node is not yet constructed (formally UB; only the pointer "
     "is stored). Upstream Fleet issue, not fleetpy. UBSan matches vptr suppressions on the "
     "target type, so this silences dynamic-type checks of casts *to Node* only."),
]


def run_checked(script, *args, timeout=None, cxx=None, suppress_known_fleet=True):
    """Run a Python script in a child process with every fleetpy model built in 'checked'
    mode (ASan + UBSan). The ASan runtime must be loaded before Python starts, which is why
    this needs a separate process. Returns the CompletedProcess; raises CheckFailed if the
    child reports a sanitizer error or exits non-zero.

    Leak detection is off: CPython itself holds allocations at exit that LSan would report.
    """
    import os
    import subprocess
    import sys
    from .build import _cxx
    cc = cxx or _cxx()
    asan = subprocess.run([cc, "-print-file-name=libasan.so"], capture_output=True, text=True).stdout.strip()
    ubsan = subprocess.run([cc, "-print-file-name=libubsan.so"], capture_output=True, text=True).stdout.strip()
    if not os.path.isabs(asan):
        raise RuntimeError(f"{cc} has no libasan.so")
    env = dict(os.environ)
    env["LD_PRELOAD"] = " ".join(x for x in (asan, ubsan, env.get("LD_PRELOAD", "")) if x)
    env["FLEETPY_MODE"] = "checked"
    pkg_parent = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    env["PYTHONPATH"] = os.pathsep.join(x for x in (pkg_parent, env.get("PYTHONPATH", "")) if x)
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1:" + env.get("ASAN_OPTIONS", "")
    ub = "print_stacktrace=1:halt_on_error=1:"
    if suppress_known_fleet and KNOWN_FLEET_UBSAN:
        import tempfile
        sup = tempfile.NamedTemporaryFile("w", suffix=".supp", delete=False)
        sup.write("\n".join(x for x, _ in KNOWN_FLEET_UBSAN) + "\n")
        sup.close()
        ub += f"suppressions={sup.name}:"
    env["UBSAN_OPTIONS"] = ub + env.get("UBSAN_OPTIONS", "")
    p = subprocess.run([sys.executable, str(script), *map(str, args)], env=env, capture_output=True,
                       text=True, timeout=timeout)
    out = p.stdout + p.stderr
    if p.returncode != 0 or "runtime error:" in out or "AddressSanitizer" in out:
        raise CheckFailed(f"checked run of {script} failed (exit {p.returncode}):\n{out[-6000:]}")
    return p
