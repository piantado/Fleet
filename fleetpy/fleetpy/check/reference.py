"""
The reference semantics of transpiled Python, as executable Python.

The user's own source is run by CPython after an AST rewrite that routes every integer
operation through ``Int64``, which checks it against int64 (fleetpy's documented integer
semantics), and every ``range`` loop through the same per-call iteration cap the C++ uses.
Nothing else changes: floats, strings, comparisons and control flow are CPython's own.
This is an independent implementation of the semantics: it shares no code with the C++
path (transpile.py + cpp/fleetpy_pyops.h), which is why comparing the two finds bugs.

``Unbounded`` is the same rewrite with Python's unbounded ints, to report how often int64
makes a difference.
"""

from __future__ import annotations

import ast
import functools
import inspect
import operator
import textwrap

from .. import ftypes as T

I64_LO, I64_HI = -(1 << 63), (1 << 63) - 1
EXACT = 1 << 53     # int/int true division is exact in double up to here

OPS = {"+": operator.add, "-": operator.sub, "*": operator.mul, "/": operator.truediv,
       "//": operator.floordiv, "%": operator.mod, "**": operator.pow, "&": operator.and_,
       "|": operator.or_, "^": operator.xor, "<<": operator.lshift, ">>": operator.rshift}
_AST_OPS = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.FloorDiv: "//",
            ast.Mod: "%", ast.Pow: "**", ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^",
            ast.LShift: "<<", ast.RShift: ">>"}
# results that become ints and must be range-checked: int(), abs(), math.floor/ceil/trunc
_CHECKED_BUILTINS = ("int", "abs")
_CHECKED_MATH = ("floor", "ceil", "trunc")


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


class Int64:
    """Integer operations with int64 semantics: any int outside int64 is an OverflowError
    (-> undefined). Also holds the per-call loop counters."""
    caps = []   # [iterations, cap] per active call, innermost last

    @classmethod
    def check(cls, *vs):
        for v in vs:
            if _is_int(v) and not I64_LO <= v <= I64_HI:
                raise OverflowError("int64 overflow")

    @classmethod
    def binop(cls, op, a, b):
        cls.check(a, b)
        if op == "/" and _is_int(a) and _is_int(b) and b != 0 and (abs(a) > EXACT or abs(b) > EXACT):
            raise OverflowError("int/int beyond 2^53")
        if op == "<<" and _is_int(b) and b > 4096 and a != 0:
            raise OverflowError("shift too large")   # cannot fit int64 unless a == 0
        r = OPS[op](a, b)
        cls.check(r)
        return r

    @classmethod
    def neg(cls, a):
        cls.check(a)
        r = -a
        cls.check(r)
        return r

    @classmethod
    def call(cls, f, *args):
        cls.check(*args)
        r = f(*args)
        cls.check(r)
        return r

    @classmethod
    def range(cls, *args):
        cls.check(*args)
        for x in range(*args):        # range(..., 0) raises ValueError -> undefined, as in C++
            top = cls.caps[-1]
            top[0] += 1
            if top[0] > top[1]:
                raise OverflowError("iteration cap")
            yield x


class Unbounded(Int64):
    """Python's unbounded ints, with the same iteration caps."""
    caps = []

    @classmethod
    def check(cls, *vs):
        pass

    @classmethod
    def binop(cls, op, a, b):
        if op == "<<" and _is_int(b) and b > 4096 and a != 0:
            raise OverflowError("shift too large")   # keeps the unbounded reference finite
        return OPS[op](a, b)


def make_struct(cls, *args):
    """Struct construction as compiled: int fields are narrowed, int values given for float
    fields become floats."""
    st = cls.__fleet_type__
    vals = []
    for (n, ft), a in zip(st.fields, args):
        if ft.category == "int" and not ft.fits(a):
            raise OverflowError(f"{st.name}.{n}: {a} does not fit {ft.key}")
        vals.append(float(a) if ft.category == "float" and _is_int(a) else a)
    return cls(*vals)


class _Rewrite(ast.NodeTransformer):

    def __init__(self, structs):
        super().__init__()
        self.structs = set(structs)

    @staticmethod
    def _chk(attr, args, like):
        f = ast.Attribute(value=ast.Name("__fp_int", ast.Load()), attr=attr, ctx=ast.Load())
        return ast.copy_location(ast.Call(func=f, args=args, keywords=[]), like)

    def visit_BinOp(self, n):
        self.generic_visit(n)
        return self._chk("binop", [ast.Constant(_AST_OPS[type(n.op)]), n.left, n.right], n)

    def visit_UnaryOp(self, n):
        self.generic_visit(n)
        return self._chk("neg", [n.operand], n) if isinstance(n.op, ast.USub) else n

    def visit_AugAssign(self, n):
        self.generic_visit(n)
        val = self._chk("binop", [ast.Constant(_AST_OPS[type(n.op)]), ast.Name(n.target.id, ast.Load()), n.value], n)
        return ast.copy_location(ast.Assign(targets=[ast.Name(n.target.id, ast.Store())], value=val), n)

    def visit_For(self, n):
        self.generic_visit(n)
        it = n.iter
        if isinstance(it, ast.Call) and isinstance(it.func, ast.Name) and it.func.id == "range":
            n.iter = self._chk("range", it.args, it)
        return n

    def visit_Call(self, n):
        self.generic_visit(n)
        f = n.func
        if isinstance(f, ast.Name) and f.id in self.structs:
            return ast.copy_location(ast.Call(func=ast.Name("__fp_struct", ast.Load()), args=[f] + n.args,
                                              keywords=[]), n)
        if (isinstance(f, ast.Name) and f.id in _CHECKED_BUILTINS) or (
                isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "math"
                and f.attr in _CHECKED_MATH):
            return self._chk("call", [f] + n.args, n)
        return n


def _rewritten(fn, ints, extra_globals):
    """``fn`` recompiled from its source with the rewrite, in a copy of its globals."""
    ns = dict(fn.__globals__)
    ns.update(extra_globals)
    ns["__fp_int"], ns["__fp_struct"] = ints, make_struct
    structs = [k for k, v in ns.items() if isinstance(v, type) and isinstance(getattr(v, "__fleet_type__", None), T.StructType)]
    tree = ast.parse(textwrap.dedent(inspect.getsource(fn)))
    tree.body[0].decorator_list = []
    tree = ast.fix_missing_locations(_Rewrite(structs).visit(tree))
    exec(compile(tree, f"<reference of {fn.__qualname__}>", "exec"), ns)
    return ns, ns[fn.__name__]


def _capped(raw, ints, cap):
    @functools.wraps(raw)
    def f(*a):
        ints.caps.append([0, cap])
        try:
            return raw(*a)
        finally:
            ints.caps.pop()
    return f


class References:
    """Reference versions of a grammar's tier-2 primitives (calling each other's references),
    and of priors and likelihoods that may call them."""

    def __init__(self, grammar, checked: bool = True):
        self.ints = Int64 if checked else Unbounded
        self.primitives = {}
        namespaces = []
        for r in grammar.primitives(tier=2):
            ns, raw = _rewritten(r.fn, self.ints, {})
            namespaces.append(ns)
            self.primitives[r.name] = self._primitive(r, _capped(raw, self.ints, r.max_iters))
        for ns in namespaces:   # calls between primitives resolve to the references
            ns.update(self.primitives)

    def _primitive(self, r, f):
        @functools.wraps(f)
        def g(*a):
            # argument and return narrowing, exactly where compiled code narrows
            for t, v in zip(r.args, a):
                if not t.fits(v):
                    raise OverflowError(f"{r.name}: argument {v!r} does not fit {t.key}")
            out = f(*a)
            if out is not None and not r.ret.fits(out):
                raise OverflowError(f"{r.name}: result {out!r} does not fit {r.ret.key}")
            return float(out) if out is not None and r.ret.category == "float" and _is_int(out) else out
        return g

    def method(self, fn, max_iters: int = 1_000_000):
        """A prior or likelihood method (called with a ``self`` stand-in)."""
        _, raw = _rewritten(fn, self.ints, self.primitives)
        return _capped(raw, self.ints, max_iters)
