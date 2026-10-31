"""
Tier 2: a strict, small subset of annotated Python, compiled to C++.

Design rule: reject rather than guess. Anything outside the subset is a TranspileError that
cites the source line. Where Python and C++ differ, the generated code implements Python's
semantics (through the helpers in cpp/fleetpy_pyops.h) or turns the difference into
"undefined" (VMSRuntimeError), never into a silently different value:

  * ints: every intermediate is int64 with overflow checks (overflow -> undefined); a value
    is range-checked when narrowed to a smaller annotated type (argument, return, field).
  * // and % on ints round toward -inf, with zero checks; on floats they are rejected.
  * /: division by zero -> undefined; int/int needs |operands| <= 2^53 (beyond that Python's
    exactly rounded result can differ from double division), else undefined.
  * int-float comparisons are rejected (Python compares exactly; write float(x)).
  * math functions: a non-finite result from finite inputs (where Python raises) -> undefined.
  * strings: len, indexing and slicing count code points, not UTF-8 bytes.
  * conditions, `and`, `or`, `not` need bools (no truthiness); no arithmetic on bools.
  * a variable must be definitely assigned before use (checked statically), so Python's
    UnboundLocalError cannot become C++ reading a default value.
  * loops: only `for v in range(...)`, with a per-call iteration cap (more -> undefined).
  * falling off the end of a function is undefined (Python would return None).
  * `raise Undefined` (fleetpy.Undefined) -> undefined.

Structure: ``Transpiler`` walks one function's AST. Each expression node kind has an
``e_<Node>`` method returning an ``Expr`` (C++ code and its FType); each statement kind an
``s_<Node>`` method. Operators, builtin functions and math functions are tables, so adding
one is a table entry plus, if Python and C++ differ, a helper in fleetpy_pyops.h and its
mirror in check/reference.py.
"""

from __future__ import annotations

import ast
import dataclasses
import inspect
import math
import textwrap

from . import ftypes as T
from .hypothesis import Code
from .likelihoods import Likelihood


class TranspileError(SyntaxError):
    pass


I64, F64, BOOL, STR, CHAR = T.i64, T.f64, T.boolean, T.string, T.char


@dataclasses.dataclass(frozen=True)
class Expr:
    code: str
    type: T.FType


def is_int(t):
    return t.category == "int"      # ints and int newtypes


def is_float(t):
    return t.category == "float"


def is_num(t):
    return is_int(t) or is_float(t)


def scalar(t):
    """Newtypes behave as their base type inside expressions."""
    return t.base if isinstance(t, T.NewType) else t


def int_literal(v: int):
    if not -(1 << 63) <= v < (1 << 63):
        return None
    return Expr("std::numeric_limits<int64_t>::min()" if v == -(1 << 63) else f"int64_t({v}LL)", I64)


class DataType(T.FType):
    """``data`` in a whole-dataset likelihood: Fleet's std::span of datums."""
    category, cpp, key = "data", "data_t", "data"

    def __init__(self, g):
        self.datum = DatumType(g)


class DatumType(T.FType):
    category, cpp, key = "datum", "datum_t", "datum"

    def __init__(self, g):
        self.fields = {"input": g.input, "output": g.output, "reliability": F64, "count": F64}


# ---- operator and function tables ----------------------------------------------------------

# int op int -> int64, through a checked helper
INT_BINOPS = {ast.Add: "fp::add", ast.Sub: "fp::sub", ast.Mult: "fp::mul", ast.FloorDiv: "fp::floordiv",
              ast.Mod: "fp::mod", ast.LShift: "fp::shl", ast.RShift: "fp::shr"}
# int op int -> int64, the same in C++ and Python on two's-complement int64
INT_BITOPS = {ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^"}
# number op number (at least one float) -> double, IEEE in both languages
FLOAT_BINOPS = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*"}
COMPARISONS = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">="}

# math.f(x) -> f(x), with fp::mcheck turning "Python raises" into undefined
MATH1 = {"sqrt": "std::sqrt", "exp": "std::exp", "log": "std::log", "log2": "std::log2",
         "log10": "std::log10", "sin": "std::sin", "cos": "std::cos", "tan": "std::tan",
         "atan": "std::atan", "tanh": "std::tanh", "fabs": "std::fabs", "log1p": "std::log1p",
         "expm1": "std::expm1", "asin": "std::asin", "acos": "std::acos"}
MATH2 = {"atan2": "std::atan2", "pow": "fp::fpow"}   # CPython's atan2 never raises
MATH_ROUNDING = ("floor", "ceil", "trunc")          # -> int
MATH_PREDICATES = ("isnan", "isinf", "isfinite")
MATH_CONSTANTS = {"pi": math.pi, "e": math.e, "inf": math.inf, "tau": math.tau}
MATH_REJECTED = {"hypot": "CPython computes math.hypot with its own algorithm, not libm's, so "
                          "results differ in the last bit; write math.sqrt(a*a + b*b)",
                 "fsum": "math.fsum is exactly rounded; no libm equivalent",
                 "sumprod": "not supported", "dist": "not supported"}

RESERVED = ("math", "self", "di", "data", "fp", "len", "abs", "min", "max", "int", "float", "str")


class Transpiler:
    """Compiles one Python function body to C++."""

    def __init__(self, fn, *, kind, grammar, params, ret, stochastic=False, max_iters=1_000_000):
        self.fn, self.kind, self.grammar, self.ret = fn, kind, grammar, ret
        self.stochastic, self.max_iters = stochastic, max_iters
        self.fdef, self.src, self.line0 = _function_ast(fn)
        cv = inspect.getclosurevars(fn)
        if cv.nonlocals:
            raise TranspileError(f"{fn.__qualname__} closes over {sorted(cv.nonlocals)}; tier-2 bodies must be pure")
        self.globals = fn.__globals__
        params = dict(params)              # name -> FType
        self.env = dict(params)            # name -> FType: parameters and locals
        self.params = set(params)
        self.defined = set(params)         # definitely assigned at the current point
        self.locals = {}                   # name -> FType, declared at the top of the body
        self.statics = {}                  # module constant -> (C++ name, FType, declaration)
        self.structs = {t.name: t for t in T.definitions(*grammar.types()) if isinstance(t, T.StructType)}
        self.used_types = []               # types used in the body beyond the grammar's
        self.prims = {r.name: r for r in grammar.primitives(tier=2)}
        self.loops = 0

    def err(self, node, msg):
        ln = getattr(node, "lineno", 1)
        raise TranspileError(f"{self.fn.__name__}, line {ln + self.line0 - 1}: {msg}\n    "
                             f"{self.src[ln - 1].strip() if self.src else ''}")

    # ---- the whole body ----------------------------------------------------------------
    def compile(self, ind=1) -> str:
        self.declare_annotated(self.fdef.body)
        lines, returns = self.block(self.fdef.body, ind)
        decl = [f"\t{d}" for _, _, d in self.statics.values()]
        decl += [f"\t{t.cpp} {n}{{}};" for n, t in self.locals.items()]
        if self.loops:
            decl.append("\tint64_t fp_iters = 0;")
        tail = [] if returns else ["\tfp::undefined(); // fell off the end (Python returns None)"]
        return "\n".join(decl + lines + tail)

    def types(self) -> tuple:
        return tuple(self.used_types) + tuple(self.locals.values())

    # ---- expressions -----------------------------------------------------------------------
    def expr(self, n) -> Expr:
        method = getattr(self, "e_" + type(n).__name__, None)
        if method is None:
            self.err(n, f"unsupported expression ({type(n).__name__})")
        return method(n)

    def e_Constant(self, n):
        return self.constant(n, n.value, "literal")

    def constant(self, n, v, what):
        if isinstance(v, bool):
            return Expr("true" if v else "false", BOOL)
        if isinstance(v, int):
            e = int_literal(v)
            if e is None:
                self.err(n, f"{what} {v} does not fit in int64")
            return e
        if isinstance(v, float):
            return Expr(T.cpp_double(v), F64)
        if isinstance(v, str):
            return Expr(T.cpp_string(v), STR)
        self.err(n, f"unsupported constant {v!r}")

    def e_Name(self, n):
        if n.id in self.env:
            if n.id not in self.defined:
                self.err(n, f"variable {n.id!r} may be used before it is assigned")
            return Expr(n.id, self.env[n.id])
        if n.id in self.globals:
            return self.global_constant(n, n.id, self.globals[n.id])
        self.err(n, f"unknown name {n.id!r}")

    def global_constant(self, n, name, v):
        """A module-level constant, frozen into the C++ at build time (the generated source is
        part of the build hash, so changing the constant rebuilds)."""
        if isinstance(v, (bool, int, float, str)):
            return self.constant(n, v, f"constant {name} =")
        if isinstance(v, (list, tuple)) and v:
            if name not in self.statics:
                items = [self.global_constant(n, f"{name}[{i}]", x) for i, x in enumerate(v)]
                kinds = {e.type.key for e in items}
                if kinds == {"i64", "f64"}:
                    self.err(n, f"constant {name} mixes ints and floats; write them all as floats")
                if len(kinds) != 1 or items[0].type.key not in ("i64", "f64", "bool", "str"):
                    self.err(n, f"constant {name} must be a list or tuple of numbers, bools or strings")
                t = T.ListOf(items[0].type)
                cname = f"fp_k_{name}"
                self.statics[name] = (cname, t, f"static const {t.cpp} {cname} = {{ "
                                      + ", ".join(e.code for e in items) + " };")
            cname, t, _ = self.statics[name]
            return Expr(cname, t)
        self.err(n, f"{name!r} is not a constant fleetpy can use (numbers, bools, strings, or lists of them)")

    def e_UnaryOp(self, n):
        o = n.operand
        if isinstance(n.op, ast.USub) and isinstance(o, ast.Constant) and \
                isinstance(o.value, (int, float)) and not isinstance(o.value, bool):
            return self.constant(n, -o.value, "integer literal")   # -2^63 is a literal only this way
        a = self.expr(o)
        if isinstance(n.op, ast.Not):
            if a.type != BOOL:
                self.err(n, "`not` needs a bool (fleetpy does not use truthiness)")
            return Expr(f"(not {a.code})", BOOL)
        if is_int(a.type):
            code = {ast.USub: f"fp::neg({a.code})", ast.UAdd: f"int64_t({a.code})",
                    ast.Invert: f"(~int64_t({a.code}))"}   # ~x == -x-1 in both languages
            return Expr(code[type(n.op)], I64)
        if is_float(a.type) and isinstance(n.op, (ast.USub, ast.UAdd)):
            return Expr(f"(-{a.code})" if isinstance(n.op, ast.USub) else f"double({a.code})", F64)
        self.err(n, f"unary {type(n.op).__name__} on {a.type.key}")

    def e_BinOp(self, n):
        a, b, op = self.expr(n.left), self.expr(n.right), type(n.op)
        ta, tb = a.type, b.type
        if BOOL in (ta, tb):
            self.err(n, "arithmetic on bool is not allowed; convert with int(...) explicitly")
        if op is ast.Add and ta in (STR, CHAR) and tb in (STR, CHAR):
            return Expr(f"(fp::str({a.code}) + fp::str({b.code}))", STR)
        if op is ast.Add and isinstance(ta, T.ListOf) and ta == tb:
            return Expr(f"fp::concat({a.code}, {b.code})", ta)
        if op in INT_BITOPS or op in (ast.LShift, ast.RShift):
            if not (is_int(ta) and is_int(tb)):
                self.err(n, f"bitwise {op.__name__} needs ints, got {ta.key} and {tb.key}")
        if not (is_num(ta) and is_num(tb)):
            self.err(n, f"operator {op.__name__} on {ta.key} and {tb.key}")
        ints = is_int(ta) and is_int(tb)
        ia, ib = f"int64_t({a.code})", f"int64_t({b.code})"
        da, db = f"double({a.code})", f"double({b.code})"
        if op is ast.Pow:
            k = n.right.value if isinstance(n.right, ast.Constant) else None
            if not (T.is_int_value(k) and 0 <= k <= 64):
                self.err(n, "** needs a non-negative int literal exponent (<= 64); use math.pow for floats")
            return Expr(f"fp::ipow({ia}, {k})", I64) if ints else Expr(f"fp::fpow({da}, {float(k)!r})", F64)
        if op is ast.Div:
            return Expr(f"fp::idiv({ia}, {ib})" if ints else f"fp::fdiv({da}, {db})", F64)
        if ints and op in INT_BINOPS:
            return Expr(f"{INT_BINOPS[op]}({ia}, {ib})", I64)
        if ints and op in INT_BITOPS:
            return Expr(f"({ia} {INT_BITOPS[op]} {ib})", I64)
        if op in (ast.FloorDiv, ast.Mod):
            self.err(n, "// and % are only supported on ints (float floor-division semantics differ); "
                        "use math.floor(a / b)")
        if op in FLOAT_BINOPS:
            return Expr(f"({da} {FLOAT_BINOPS[op]} {db})", F64)
        self.err(n, f"unsupported operator {op.__name__}")

    def e_BoolOp(self, n):
        parts = [self.expr(v) for v in n.values]
        if any(p.type != BOOL for p in parts):
            self.err(n, "`and`/`or` only on bools (Python returns an operand otherwise)")
        j = " and " if isinstance(n.op, ast.And) else " or "
        return Expr("(" + j.join(p.code for p in parts) + ")", BOOL)

    def e_Compare(self, n):
        terms = [self.expr(t) for t in [n.left, *n.comparators]]
        out = []
        for op, a, b in zip(n.ops, terms, terms[1:]):
            if type(op) not in COMPARISONS:
                self.err(n, "in / is are not supported")
            sym = COMPARISONS[type(op)]
            ta, tb = a.type, b.type
            if {ta.key, tb.key} == {"char", "str"}:   # a fl.char is a one-character str in Python
                ca, cb = f"fp::str({a.code})", f"fp::str({b.code})"
            elif is_num(ta) and is_num(tb):
                if is_int(ta) != is_int(tb):
                    self.err(n, "comparing an int with a float: Python compares exactly; convert "
                                "explicitly with float(...)")
                cast = "int64_t" if is_int(ta) else "double"
                ca, cb = f"{cast}({a.code})", f"{cast}({b.code})"
            elif ta != tb:
                self.err(n, f"comparing {ta.key} with {tb.key}")
            elif sym not in ("==", "!=") and ta not in (STR, BOOL, CHAR):
                self.err(n, f"ordering comparison on {ta.key} is not supported")
            else:
                ca, cb = a.code, b.code
            out.append(f"({ca} {sym} {cb})")
        return Expr("(" + " and ".join(out) + ")", BOOL)

    def e_IfExp(self, n):
        c = self.condition(n.test, "condition")
        a, b = self.expr(n.body), self.expr(n.orelse)
        if is_num(a.type) and is_num(b.type) and is_int(a.type) == is_int(b.type):
            t = I64 if is_int(a.type) else F64
            return Expr(f"({c} ? {t.cpp}({a.code}) : {t.cpp}({b.code}))", t)
        if a.type != b.type:
            self.err(n, f"the two branches have different types ({a.type.key}, {b.type.key})")
        return Expr(f"({c} ? {a.code} : {b.code})", a.type)

    def condition(self, node, what) -> str:
        c = self.expr(node)
        if c.type != BOOL:
            self.err(node, f"{what} must be a bool (no truthiness)")
        return c.code

    def e_Attribute(self, n):
        if isinstance(n.value, ast.Name) and n.value.id == "math":
            if n.attr in MATH_CONSTANTS:
                return Expr(T.cpp_double(MATH_CONSTANTS[n.attr]), F64)
            self.err(n, f"math.{n.attr} is not supported as a value")
        base = self.expr(n.value)
        if isinstance(base.type, T.StructType):
            ft = base.type.field(n.attr)
            if ft is None:
                self.err(n, f"{base.type.name} has no field {n.attr!r}")
            return Expr(f"{base.code}.{n.attr}", ft)
        if isinstance(base.type, DatumType):
            if n.attr not in base.type.fields:
                self.err(n, f"a datum has no field {n.attr!r}")
            return Expr(f"{base.code}.{n.attr}", base.type.fields[n.attr])
        self.err(n, f"attribute access on {base.type.key}")

    def e_Subscript(self, n):
        base = self.expr(n.value)
        tb = base.type
        if isinstance(tb, T.TupleOf):
            i = n.slice.value if isinstance(n.slice, ast.Constant) else None
            if not T.is_int_value(i):
                self.err(n, "tuple index must be an int literal")
            if not -len(tb.elems) <= i < len(tb.elems):
                self.err(n, "tuple index out of range")
            i %= len(tb.elems)
            return Expr(f"std::get<{i}>({base.code})", tb.elems[i])
        if isinstance(n.slice, ast.Slice):
            if n.slice.step is not None:
                self.err(n, "slices with a step are not supported")
            bounds = []
            for e in (n.slice.lower, n.slice.upper):
                if e is None:
                    bounds += ["false", "int64_t(0)"]
                else:
                    be = self.expr(e)
                    if not is_int(be.type):
                        self.err(n, "slice bounds must be ints")
                    bounds += ["true", f"int64_t({be.code})"]
            if tb == STR:
                fn = "fp::cp_slice"      # code points
            elif isinstance(tb, T.ListOf):
                fn = "fp::slice"
            else:
                self.err(n, f"slicing {tb.key}")
            return Expr(f"{fn}({base.code}, {', '.join(bounds)})", tb)
        idx = self.expr(n.slice)
        if not is_int(idx.type):
            self.err(n, "index must be an int")
        i = f"int64_t({idx.code})"
        if isinstance(tb, T.ListOf):
            return Expr(f"fp::index({base.code}, {i})", tb.elem)
        if isinstance(tb, DataType):
            return Expr(f"fp::index({base.code}, {i})", tb.datum)
        if tb == STR:
            return Expr(f"fp::cp_index({base.code}, {i})", STR)
        self.err(n, f"indexing {tb.key}")

    def e_List(self, n):
        items = self.items(n)
        if any(e.type != items[0].type for e in items):
            self.err(n, "list elements must all have the same type")
        t = T.ListOf(items[0].type)
        return Expr(t.cpp + "{" + ", ".join(e.code for e in items) + "}", t)

    def e_Tuple(self, n):
        items = self.items(n)
        t = T.TupleOf([e.type for e in items])
        return Expr(t.cpp + "{" + ", ".join(e.code for e in items) + "}", t)

    def items(self, n):
        if not n.elts:
            self.err(n, "empty literals need a declared type: write `xs: list[int] = []`")
        return [self.expr(e) for e in n.elts]

    # ---- calls ---------------------------------------------------------------------------
    def e_Call(self, n):
        if n.keywords:
            self.err(n, "keyword arguments are not supported")
        args = [self.expr(a) for a in n.args]
        f = n.func
        if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "self":
            return self.self_method(n, f.attr, args)
        if isinstance(f, ast.Attribute) and f.attr in ("bit_count", "bit_length"):
            base = self.expr(f.value)
            if not is_int(base.type) or args:
                self.err(n, f".{f.attr}() is an int method with no arguments")
            return Expr(f"fp::{f.attr}(int64_t({base.code}))", I64)
        if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "math":
            return self.math_call(n, f.attr, args)
        if not isinstance(f, ast.Name):
            self.err(n, "only calls to builtins, math functions, structs and other tier-2 primitives are supported")
        handler = getattr(self, "f_" + f.id, None)
        if handler is not None:
            return handler(n, args)
        st = self.struct(f.id)
        if st is not None:
            if len(args) != len(st.fields):
                self.err(n, f"{f.id} takes {len(st.fields)} arguments")
            parts = [self.coerce(n, a, ft) for a, (_, ft) in zip(args, st.fields)]
            return Expr(f"{f.id}(" + ", ".join(parts) + ")", st)
        if f.id in self.prims:
            p = self.prims[f.id]
            if len(args) != p.arity:
                self.err(n, f"{f.id} takes {p.arity} arguments")
            return Expr(f"{f.id}(" + ", ".join(self.coerce(n, a, t) for a, t in zip(args, p.args)) + ")", p.ret)
        self.err(n, f"unknown function {f.id!r} (only tier-2 primitives can be called; tier-1 bodies "
                    f"have no Python version to test against)")

    def struct(self, name):
        if name not in self.structs:
            cls = self.globals.get(name)
            ft = getattr(cls, "__fleet_type__", None) if isinstance(cls, type) else None
            if not isinstance(ft, T.StructType):
                return None
            self.structs[name] = ft
            self.used_types.append(ft)
        return self.structs[name]

    def self_method(self, n, name, args):
        """``self.<name>(...)``: what a hypothesis offers depends on what is being compiled."""
        if self.kind == "prior":
            calls = {"grammar_prior": Expr("this->Super::compute_prior()", F64),
                     "recursion_count": Expr("int64_t(this->recursion_count())", I64),
                     "nodes": Expr("int64_t(this->value.count())", I64)}
            if name not in calls:
                self.err(n, "a prior may call self.grammar_prior(), self.recursion_count() and self.nodes()")
            if args:
                self.err(n, f"self.{name}() takes no arguments")
            return calls[name]
        if self.kind != "likelihood" or name not in ("call", "logprob"):
            self.err(n, "only self.call(x) (deterministic) and self.logprob(x, y) (stochastic) are "
                        "supported on self, inside a likelihood")
        g = self.grammar
        if name == "call":
            if self.stochastic:
                self.err(n, "a stochastic hypothesis outputs a distribution; use self.logprob(x, y)")
            if len(args) not in (1, 2):
                self.err(n, "self.call takes (x) or (x, default)")
            x = self.coerce(n, args[0], g.input)
            if len(args) == 1:
                return Expr(f"fp_callx({x})", g.output)
            d = self.coerce(n, args[1], g.output)
            # undefined -> default (Fleet's call(x, err) returns err on error)
            return Expr(f"([&]() {{ auto [fp_o, fp_v] = this->fp_call({x}); "
                        f"return fp_o ? fp_v : {g.output.cpp}({d}); }}())", g.output)
        if not self.stochastic:
            self.err(n, "self.logprob is for stochastic hypotheses; use self.call(x)")
        if len(args) != 2:
            self.err(n, "self.logprob takes (x, y)")
        if args[0].type != g.input or args[1].type != g.output:
            self.err(n, f"self.logprob expects ({g.input.key}, {g.output.key})")
        return Expr(f"this->fp_logprob({args[0].code}, {args[1].code})", F64)

    def math_call(self, n, name, args):
        def numbers(k):
            if len(args) != k or not all(is_num(a.type) for a in args):
                self.err(n, f"math.{name} takes {'one number' if k == 1 else 'two numbers'}")
            return [f"double({a.code})" for a in args]
        if name in MATH_REJECTED:
            self.err(n, f"math.{name}: {MATH_REJECTED[name]}")
        if name in MATH1:
            (a,) = numbers(1)
            return Expr(f"fp::mcheck({MATH1[name]}({a}), {a})", F64)
        if name in MATH2:
            a, b = numbers(2)
            return Expr(f"{MATH2[name]}({a}, {b})", F64)
        if name in MATH_PREDICATES:
            (a,) = numbers(1)
            return Expr(f"std::{name}({a})", BOOL)
        if name in MATH_ROUNDING:
            if len(args) != 1:
                self.err(n, f"math.{name} takes one argument")
            (a,) = args
            if is_int(a.type):
                return Expr(f"int64_t({a.code})", I64)
            if is_float(a.type):
                return Expr(f"fp::to_int(std::{name}(double({a.code})))", I64)
            self.err(n, f"math.{name} on {a.type.key}")
        self.err(n, f"math.{name} is not supported")

    # builtin functions: f_<name>(node, args) -> Expr
    def one(self, n, args, name):
        if len(args) != 1:
            self.err(n, f"{name} takes one argument")
        return args[0]

    def f_str(self, n, args):
        if len(args) != 1 or args[0].type not in (STR, CHAR):
            self.err(n, "str() is only supported on str and fl.char")
        return Expr(f"fp::str({args[0].code})", STR)

    def f_len(self, n, args):
        a = self.one(n, args, "len")
        if a.type == STR:
            return Expr(f"int64_t(fp::cp_len({a.code}))", I64)
        if isinstance(a.type, (T.ListOf, T.SetOf, DataType)):
            return Expr(f"int64_t(({a.code}).size())", I64)
        self.err(n, f"len of {a.type.key}")

    def f_abs(self, n, args):
        a = self.one(n, args, "abs")
        if is_int(a.type):
            return Expr(f"fp::iabs(int64_t({a.code}))", I64)
        if is_float(a.type):
            return Expr(f"std::fabs(double({a.code}))", F64)
        self.err(n, f"abs of {a.type.key}")

    def minmax(self, n, args, name):
        """Python's min/max compare with < and > and keep the first argument on ties (so with
        a NaN the result depends on the argument order, exactly as fp::minv / fp::maxv)."""
        if len(args) != 2:
            self.err(n, f"{name} takes exactly two arguments")
        a, b = args
        if is_num(a.type) and is_num(b.type):
            if is_int(a.type) != is_int(b.type):
                self.err(n, f"{name} of an int and a float: convert explicitly")
            t = I64 if is_int(a.type) else F64
            return Expr(f"fp::{name}v({t.cpp}({a.code}), {t.cpp}({b.code}))", t)
        if a.type != b.type or a.type != STR:
            self.err(n, f"{name} on {a.type.key}, {b.type.key}")
        return Expr(f"fp::{name}v({a.code}, {b.code})", STR)

    def f_min(self, n, args):
        return self.minmax(n, args, "min")

    def f_max(self, n, args):
        return self.minmax(n, args, "max")

    def f_float(self, n, args):
        if len(args) != 1 or not is_num(args[0].type):
            self.err(n, "float() takes one number")
        return Expr(f"double({args[0].code})", F64)

    def f_int(self, n, args):
        a = self.one(n, args, "int()")
        if a.type == BOOL:
            return Expr(f"int64_t({a.code} ? 1 : 0)", I64)
        if is_int(a.type):
            return Expr(f"int64_t({a.code})", I64)
        if is_float(a.type):
            return Expr(f"fp::to_int(double({a.code}))", I64)
        self.err(n, f"int() of {a.type.key}")

    def f_bool(self, n, args):
        self.err(n, "bool() uses truthiness; write an explicit comparison")

    def f_Undefined(self, n, args):
        self.err(n, "Undefined can only be raised")

    # ---- conversions ---------------------------------------------------------------------
    def coerce(self, node, e: Expr, want: T.FType) -> str:
        """Convert ``e`` to type ``want`` with Python-compatible checks (no implicit
        conversion that Python would not make)."""
        have, code = e.type, e.code
        if have == want:
            return code
        if is_int(want) and is_int(have):
            v = f"fp::narrow<{scalar(want).cpp}>(int64_t({code}))"
            return f"{want.cpp}({v})" if isinstance(want, T.NewType) else v
        if is_float(want) and is_float(have):
            return f"{want.cpp}(double({code}))"
        if is_float(want) and is_int(have):   # Python's float(int) rounds to nearest, as (double)int64_t
            return f"{want.cpp}(double(int64_t({code})))"
        if want == STR and have == CHAR:
            return f"fp::str({code})"
        if isinstance(want, T.NewType) and scalar(want) == have:
            return f"{want.cpp}({code})"
        if isinstance(have, T.NewType) and scalar(have) == want:
            return f"{scalar(have).cpp}({code})"
        self.err(node, f"expected {want.key}, got {have.key} (no implicit conversion)")

    # ---- locals --------------------------------------------------------------------------
    def declare_annotated(self, body):
        """First pass: every `name: T = ...` declares name with type T."""
        import fleetpy
        ns = dict(self.globals)
        ns.update({"int": int, "float": float, "bool": bool, "str": str, "list": list, "tuple": tuple,
                   "frozenset": frozenset, "fl": fleetpy, "fleetpy": fleetpy})
        ns.update({name: st.pycls for name, st in self.structs.items()})
        ns.update({k: getattr(T, k) for k in ("i8", "i16", "i32", "i64", "f64")})
        for st in ast.walk(ast.Module(body=body, type_ignores=[])):
            if isinstance(st, ast.AnnAssign):
                if not isinstance(st.target, ast.Name):
                    self.err(st, "only simple names can be assigned")
                try:
                    t = T.resolve(eval(compile(ast.Expression(st.annotation), "<ann>", "eval"), ns))
                except Exception as e:  # noqa: BLE001
                    self.err(st, f"cannot resolve annotation: {e}")
                self.declare(st, st.target.id, t)

    def declare(self, node, name, t):
        if name in self.params:
            self.err(node, f"assigning to parameter {name!r} is not allowed")
        if name in RESERVED:
            self.err(node, f"{name!r} is reserved")
        T.check_identifier(name, "local variable")
        if name in self.locals and self.locals[name] != t:
            self.err(node, f"{name!r} was {self.locals[name].key}, cannot become {t.key}")
        self.locals[name] = self.env[name] = t

    def assigned_type(self, node, name, t):
        """A local's type: its declaration, else that of its first assignment (with ints
        widened to i64, as Python ints are unbounded)."""
        if name not in self.locals:
            self.declare(node, name, I64 if is_int(t) and not isinstance(t, T.NewType) else t)
        return self.locals[name]

    # ---- statements ------------------------------------------------------------------------
    def block(self, body, ind):
        """Compile statements; returns (lines, always_returns). Updates self.defined."""
        out = []
        for st in body:
            method = getattr(self, "s_" + type(st).__name__, None)
            if method is None:
                self.err(st, f"unsupported statement ({type(st).__name__})")
            lines, ends = method(st, "\t" * ind, ind)
            out += lines
            if ends is not None:     # return/raise (True) or break/continue (False): the rest is unreachable
                return out, ends
        return out, False

    def s_Return(self, st, pad, ind):
        if st.value is None:
            self.err(st, "return needs a value")
        return [f"{pad}return {self.coerce(st, self.expr(st.value), self.ret)};"], True

    def s_Raise(self, st, pad, ind):
        ex = st.exc.func if isinstance(st.exc, ast.Call) else st.exc
        name = ex.id if isinstance(ex, ast.Name) else (ex.attr if isinstance(ex, ast.Attribute) else None)
        if name != "Undefined":
            self.err(st, "only `raise Undefined` (fleetpy.Undefined) is supported")
        return [f"{pad}fp::undefined();"], True

    def s_Assign(self, st, pad, ind):
        if len(st.targets) != 1 or not isinstance(st.targets[0], ast.Name):
            self.err(st, "only `name = expr` assignments are supported")
        return self.assign(st, st.targets[0].id, st.value, pad), None

    def s_AnnAssign(self, st, pad, ind):
        if st.value is None:
            self.err(st, "annotated declarations need a value")
        name = st.target.id
        if isinstance(st.value, ast.List) and not st.value.elts:   # xs: list[T] = []
            self.defined.add(name)
            return [f"{pad}{name} = {self.locals[name].cpp}{{}};"], None
        return self.assign(st, name, st.value, pad), None

    def s_AugAssign(self, st, pad, ind):
        if not isinstance(st.target, ast.Name):
            self.err(st, "only names can be augmented")
        name = st.target.id
        if name in self.params:
            self.err(st, f"assigning to parameter {name!r} is not allowed")
        value = ast.BinOp(left=ast.Name(id=name, ctx=ast.Load(), lineno=st.lineno), op=st.op,
                          right=st.value, lineno=st.lineno)
        return self.assign(st, name, value, pad), None

    def assign(self, st, name, value, pad):
        e = self.expr(value)
        t = self.assigned_type(st, name, e.type)
        self.defined.add(name)
        return [f"{pad}{name} = {self.coerce(st, e, t)};"]

    def s_If(self, st, pad, ind):
        cond = self.condition(st.test, "if-condition")
        before = set(self.defined)
        a, ra = self.block(st.body, ind + 1)
        da, self.defined = self.defined, set(before)
        b, rb = self.block(st.orelse, ind + 1) if st.orelse else ([], False)
        db = self.defined
        out = [f"{pad}if({cond}) {{", *a, f"{pad}}}"]
        if st.orelse:
            out += [f"{pad}else {{", *b, f"{pad}}}"]
        if ra and rb:
            return out, True
        # definitely assigned after the if: through every branch that can fall through
        self.defined = db if ra else da if rb else da & db
        return out, None

    def s_For(self, st, pad, ind):
        if st.orelse:
            self.err(st, "for/else is not supported")
        if not isinstance(st.target, ast.Name):
            self.err(st, "loop variable must be a name")
        it = st.iter
        if not (isinstance(it, ast.Call) and isinstance(it.func, ast.Name) and it.func.id == "range"
                and 1 <= len(it.args) <= 3 and not it.keywords):
            self.err(st, "only `for i in range(...)` loops are supported")
        bounds = []
        for a in it.args:
            e = self.expr(a)
            if not is_int(e.type):
                self.err(st, "range() arguments must be ints")
            bounds.append(f"int64_t({e.code})")
        lo, hi, step = (["int64_t(0)", *bounds, "int64_t(1)"] if len(bounds) == 1 else
                        [*bounds, "int64_t(1)"] if len(bounds) == 2 else bounds)
        v = st.target.id
        if v in self.locals and self.locals[v] != I64:
            self.err(st, f"{v!r} is not an int")
        self.declare(st, v, I64)
        k = self.loops
        self.loops += 1
        # iterate on a count, so that the loop variable itself can never overflow
        out = [f"{pad}{{ const int64_t fp_lo{k} = {lo}, fp_hi{k} = {hi}, fp_st{k} = {step};",
               f"{pad}  if(fp_st{k} == 0) fp::undefined(); // range() arg 3 must not be zero",
               f"{pad}  for(int64_t fp_i{k} = 0; ; fp_i{k}++) {{",
               f"{pad}\t__int128 fp_v{k} = (__int128)fp_lo{k} + (__int128)fp_st{k} * fp_i{k};",
               f"{pad}\tif(fp_st{k} > 0 ? fp_v{k} >= fp_hi{k} : fp_v{k} <= fp_hi{k}) break;",
               f"{pad}\tif(++fp_iters > {self.max_iters}LL) fp::undefined(); // iteration cap",
               f"{pad}\t{v} = (int64_t)fp_v{k};"]
        before = set(self.defined)
        self.defined.add(v)
        body, _ = self.block(st.body, ind + 1)
        self.defined = before   # the loop may run zero times: nothing in it is definitely assigned
        return out + body + [f"{pad}  }}", f"{pad}}}"], None

    def s_Pass(self, st, pad, ind):
        return [], None

    def s_Break(self, st, pad, ind):
        return [f"{pad}break;"], False

    def s_Continue(self, st, pad, ind):
        return [f"{pad}continue;"], False

    def s_Expr(self, st, pad, ind):
        v = st.value
        if isinstance(v, ast.Constant) and isinstance(v.value, str):
            return [], None   # docstring
        if isinstance(v, ast.Constant) and v.value is Ellipsis:
            self.err(st, "a body of `...` has no implementation: give cpp= (tier 1) or write the body")
        self.err(st, f"unsupported statement ({type(st).__name__})")


def _function_ast(fn):
    try:
        src = textwrap.dedent(inspect.getsource(fn))
    except (OSError, TypeError) as e:
        raise TranspileError(f"cannot read the source of {fn.__qualname__} ({e}); tier-2 code "
                             f"must be defined in a file") from None
    fdefs = [n for n in ast.parse(src).body if isinstance(n, ast.FunctionDef)]
    if len(fdefs) != 1:
        raise TranspileError(f"expected one function definition for {fn.__qualname__}")
    return fdefs[0], src.splitlines(), fn.__code__.co_firstlineno


def _undefined_is_minus_infinity(body: str) -> str:
    return f"try {{\n{body}\n}} catch (VMSRuntimeError&) {{\n\treturn -infinity;\n}}"


# ---- entry points ------------------------------------------------------------------------------

def primitive(r, grammar) -> Code:
    """The C++ body of tier-2 primitive ``r``."""
    t = Transpiler(r.fn, kind="primitive", grammar=grammar, params=r.params, ret=r.ret, max_iters=r.max_iters)
    return Code(t.compile(), transpiled=True, pyfn=r.fn, types=t.types())


def prior(fn, grammar) -> Code:
    """The body of compute_prior for an @fl.prior method; undefined -> -inf."""
    t = Transpiler(fn, kind="prior", grammar=grammar, params={}, ret=F64)
    if [a.arg for a in t.fdef.args.args] != ["self"]:
        raise TranspileError("a prior must be `def name(self) -> float`")
    return Code(_undefined_is_minus_infinity(t.compile()), transpiled=True, pyfn=fn, types=t.types())


def likelihood(pl, grammar, *, stochastic: bool) -> Likelihood:
    """A Likelihood from an @fl.likelihood method; undefined (anywhere) -> -inf."""
    arg = "data" if pl.total else "di"
    params = {arg: DataType(grammar) if pl.total else DatumType(grammar)}
    t = Transpiler(pl.fn, kind="likelihood", grammar=grammar, params=params, ret=F64, stochastic=stochastic)
    a = t.fdef.args
    if [x.arg for x in a.args] != ["self", arg] or a.vararg or a.kwonlyargs:
        raise TranspileError(f"a likelihood must be `def name(self, {arg}) -> float`")
    body = t.compile(ind=2)
    IN, OUT = grammar.input.cpp, grammar.output.cpp
    callx = "" if stochastic else (f"auto fp_callx = [this](const {IN}& fp_x) -> {OUT} {{\n"
                                   f"\tauto [fp_ok, fp_out] = this->fp_call(fp_x);\n"
                                   f"\tif(not fp_ok) fp::undefined();\n"
                                   f"\treturn fp_out;\n}};\n")
    return Likelihood(callx + _undefined_is_minus_infinity(body), pl.nonpositive,
                      "transpiled:" + pl.fn.__qualname__, "dist" if stochastic else "value",
                      scope="total" if pl.total else "datum", transpiled=True, pyfn=pl.fn, types=t.types())
