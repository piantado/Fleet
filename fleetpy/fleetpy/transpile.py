"""
Tier 2: a strict, small subset of annotated Python compiled to C++.

Design rule: reject rather than guess. Anything outside the subset is a TranspileError that
cites the source line. Where Python and C++ semantics differ, the generated code implements
Python's semantics, or turns the difference into "undefined" (VMSRuntimeError), never into a
silently different value:

  * integers: every intermediate is int64 with overflow checks (overflow -> undefined); a
    value is range-checked when narrowed to a smaller annotated type (argument or return).
  * // and % on ints round toward -inf (Python), with zero checks; on floats they are rejected.
  * /: ZeroDivisionError -> undefined; int/int requires both |operands| <= 2^53 (beyond that
    Python's exact-rational rounding can differ from double division) else undefined.
  * comparisons between int and float are rejected (Python compares exactly; write float(x)).
  * math functions: a non-finite result from finite inputs (Python raises) -> undefined.
  * strings: len and indexing count code points (Python), not UTF-8 bytes.
  * conditions must be bool (no truthiness); `and`/`or` only on bools.
  * a variable must be definitely assigned before use (static check), so Python's
    UnboundLocalError cannot turn into C++ reading a default value.
  * falling off the end of a function is undefined (Python would return None).
  * raise Undefined (or fleetpy.Undefined) -> undefined.
"""

from __future__ import annotations

import ast
import inspect
import math
import textwrap

from . import ftypes as T


class TranspileError(SyntaxError):
    pass


I64 = T.i64
F64 = T.f64
BOOL = T.boolean
STR = T.string
CHAR = T.char

PRELUDE = r"""
// ---- fleetpy tier-2 runtime helpers (Python semantics; undefined -> VMSRuntimeError) ----
namespace fp {
	[[noreturn]] inline void undefined() { throw VMSRuntimeError(); }

	template<typename T> inline T narrow(int64_t v) {
		if constexpr (std::is_same_v<T, int64_t>) { return v; }
		else {
			if(v < (int64_t)std::numeric_limits<T>::min() or v > (int64_t)std::numeric_limits<T>::max()) undefined();
			return (T)v;
		}
	}
	inline int64_t add(int64_t a, int64_t b) { int64_t r; if(__builtin_add_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t sub(int64_t a, int64_t b) { int64_t r; if(__builtin_sub_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t mul(int64_t a, int64_t b) { int64_t r; if(__builtin_mul_overflow(a, b, &r)) undefined(); return r; }
	inline int64_t neg(int64_t a) { if(a == std::numeric_limits<int64_t>::min()) undefined(); return -a; }
	inline int64_t iabs(int64_t a) { return a < 0 ? neg(a) : a; }
	inline int64_t floordiv(int64_t a, int64_t b) {
		if(b == 0) undefined();
		if(a == std::numeric_limits<int64_t>::min() and b == -1) undefined();
		int64_t q = a / b;
		if((a % b != 0) and ((a < 0) != (b < 0))) q -= 1;
		return q;
	}
	inline int64_t mod(int64_t a, int64_t b) {
		if(b == 0) undefined();
		if(b == -1) return 0; // avoids INT64_MIN % -1 (UB); Python gives 0
		int64_t r = a % b;
		if(r != 0 and ((r < 0) != (b < 0))) r += b;
		return r;
	}
	inline int64_t ipow(int64_t a, int64_t k) { // k >= 0, checked by the transpiler
		int64_t r = 1;
		for(int64_t i = 0; i < k; i++) r = mul(r, a);
		return r;
	}
	inline double fdiv(double a, double b) { if(b == 0.0) undefined(); return a / b; }
	inline double idiv(int64_t a, int64_t b) {
		if(b == 0) undefined();
		const int64_t L = (int64_t)1 << 53;
		if(a > L or a < -L or b > L or b < -L) undefined();
		return (double)a / (double)b;
	}
	inline double fpow(double a, double b) {
		if(a == 0.0 and b < 0.0) undefined();                       // ZeroDivisionError
		double r = std::pow(a, b);
		if(std::isinf(r) and std::isfinite(a) and std::isfinite(b)) undefined(); // OverflowError
		if(std::isnan(r) and not std::isnan(a) and not std::isnan(b)) undefined(); // complex result
		return r;
	}
	template<typename... A> inline double mcheck(double r, A... in) {
		const bool anynan = (std::isnan((double)in) or ...);
		const bool allfinite = (std::isfinite((double)in) and ...);
		if(std::isnan(r) and not anynan) undefined();
		if(std::isinf(r) and allfinite) undefined();
		return r;
	}
	inline int64_t to_int(double x) { // Python int(float): truncation; nan/inf/out of range -> undefined
		if(std::isnan(x) or std::isinf(x)) undefined();
		double t = std::trunc(x);
		if(t < -9223372036854775808.0 or t >= 9223372036854775808.0) undefined();
		return (int64_t)t;
	}
	inline size_t cp_len(const std::string& s) {
		size_t n = 0;
		for(unsigned char c : s) n += ((c & 0xC0) != 0x80);
		return n;
	}
	inline std::string cp_index(const std::string& s, int64_t i) {
		int64_t n = (int64_t)cp_len(s);
		if(i < 0) i += n;
		if(i < 0 or i >= n) undefined();
		size_t k = 0; int64_t cp = -1; size_t start = 0;
		for(; k < s.size(); k++) {
			if(((unsigned char)s[k] & 0xC0) != 0x80) { cp++; if(cp == i) { start = k; break; } }
		}
		size_t end = start + 1;
		while(end < s.size() and (((unsigned char)s[end] & 0xC0) == 0x80)) end++;
		return s.substr(start, end - start);
	}
	template<typename V> inline const typename V::value_type& index(const V& v, int64_t i) {
		int64_t n = (int64_t)v.size();
		if(i < 0) i += n;
		if(i < 0 or i >= n) undefined();
		return v[(size_t)i];
	}
	template<typename V> inline V concat(const V& a, const V& b) { V r = a; r.insert(r.end(), b.begin(), b.end()); return r; }
	// Python slice bounds: negative indices count from the end, then clamp to [0, n]
	inline int64_t slice_bound(bool has, int64_t i, int64_t n, int64_t dflt) {
		if(not has) return dflt;
		if(i < 0) { i += n; if(i < 0) i = 0; }
		else if(i > n) i = n;
		return i;
	}
	template<typename V> inline V slice(const V& v, bool hl, int64_t lo, bool hh, int64_t hi) {
		const int64_t n = (int64_t)v.size();
		lo = slice_bound(hl, lo, n, 0); hi = slice_bound(hh, hi, n, n);
		if(hi <= lo) return V{};
		return V(v.begin() + lo, v.begin() + hi);
	}
	inline size_t cp_offset(const std::string& s, int64_t cp) { // byte offset of code point cp (or size())
		int64_t k = -1;
		for(size_t b = 0; b < s.size(); b++) {
			if(((unsigned char)s[b] & 0xC0) != 0x80) { k++; if(k == cp) return b; }
		}
		return s.size();
	}
	inline std::string cp_slice(const std::string& s, bool hl, int64_t lo, bool hh, int64_t hi) {
		const int64_t n = (int64_t)cp_len(s);
		lo = slice_bound(hl, lo, n, 0); hi = slice_bound(hh, hi, n, n);
		if(hi <= lo) return std::string();
		const size_t a = cp_offset(s, lo), b = cp_offset(s, hi);
		return s.substr(a, b - a);
	}
	inline std::string str(char c) { return std::string(1, c); }
	inline const std::string& str(const std::string& x) { return x; }
	// Python's << and >> on int64 values: negative shift counts raise; results must fit int64
	inline int64_t shl(int64_t a, int64_t n) {
		if(n < 0) undefined();
		if(a == 0) return 0;
		if(n >= 64) undefined();
		const __int128 r = (__int128)a << n;
		if(r > (__int128)std::numeric_limits<int64_t>::max() or r < (__int128)std::numeric_limits<int64_t>::min()) undefined();
		return (int64_t)r;
	}
	inline int64_t shr(int64_t a, int64_t n) {
		if(n < 0) undefined();
		if(n >= 64) return a < 0 ? -1 : 0;
		return a >> n;  // arithmetic shift (floor), as Python
	}
	inline uint64_t uabs(int64_t a) { return a < 0 ? (uint64_t)0 - (uint64_t)a : (uint64_t)a; }
	inline int64_t bit_count(int64_t a)  { return __builtin_popcountll(uabs(a)); }
	inline int64_t bit_length(int64_t a) { const uint64_t u = uabs(a); return u == 0 ? 0 : 64 - __builtin_clzll(u); }
	template<typename T> inline T minv(T a, T b) { return (b < a) ? b : a; } // Python min: first wins ties
	template<typename T> inline T maxv(T a, T b) { return (b > a) ? b : a; } // Python max: first wins ties
}
"""

_MATH1 = {"sqrt": "std::sqrt", "exp": "std::exp", "log": "std::log", "log2": "std::log2",
          "log10": "std::log10", "sin": "std::sin", "cos": "std::cos", "tan": "std::tan",
          "atan": "std::atan", "tanh": "std::tanh", "fabs": "std::fabs", "log1p": "std::log1p",
          "expm1": "std::expm1", "asin": "std::asin", "acos": "std::acos"}
# CPython's math.atan2 never raises; math.hypot uses CPython's own algorithm (not libm), so it
# cannot be matched bit-for-bit and is rejected below.
_MATH2 = {"atan2": "std::atan2", "pow": "fp::fpow"}
_UNSUPPORTED_MATH = {"hypot": "CPython computes math.hypot with its own algorithm, not libm's, so "
                              "results differ in the last bit; write math.sqrt(a*a + b*b)",
                     "fsum": "math.fsum is exactly rounded; no libm equivalent",
                     "sumprod": "not supported", "dist": "not supported"}
_MATHCONST = {"pi": math.pi, "e": math.e, "inf": math.inf, "tau": math.tau}


def _is_int(t):
    return isinstance(t, T._Int) or (isinstance(t, T.NewType) and isinstance(t.base, T._Int))


def _is_float(t):
    return isinstance(t, T._Float) or (isinstance(t, T.NewType) and isinstance(t.base, T._Float))


def _is_num(t):
    return _is_int(t) or _is_float(t)


def _scalar(t):
    """Newtypes behave like their base type inside expressions."""
    return t.base if isinstance(t, T.NewType) else t


def _flit(v: float) -> str:
    if math.isnan(v):
        return "std::numeric_limits<double>::quiet_NaN()"
    if math.isinf(v):
        return ("" if v > 0 else "-") + "std::numeric_limits<double>::infinity()"
    return f"double({float.hex(v)})"


def _ilit(v: int) -> str:
    if not (-(1 << 63) <= v < (1 << 63)):
        raise ValueError("integer literal does not fit in int64")
    if v == -(1 << 63):
        return "std::numeric_limits<int64_t>::min()"
    return f"int64_t({v}LL)"


class _Ctx:
    def __init__(self, fn_name, src_lines, line0, env, ret, prims, structs, kind="primitive", grammar=None):
        self.fn_name = fn_name
        self.src_lines = src_lines
        self.line0 = line0
        self.env = dict(env)        # name -> FType (params + locals)
        self.params = set(env)
        self.ret = ret
        self.prims = prims          # name -> Primitive (tier 2 only, callable)
        self.structs = structs      # name -> StructType
        self.locals = {}            # name -> FType (declared at top of function)
        self.kind = kind
        self.grammar = grammar
        self.tmp = 0
        self.globals = {}           # the Python function's globals (to find @fl.struct classes)
        self.extra_structs = []     # struct types used in the body but not by any grammar rule
        self.max_iters = 1_000_000  # loop iterations allowed per call; more -> undefined
        self.stochastic = False     # likelihood of a stochastic hypothesis
        self.statics = {}           # module-level list/tuple constants -> (static C++ name, decl)

    def err(self, node, msg):
        ln = getattr(node, "lineno", 1) + self.line0 - 1
        src = self.src_lines[getattr(node, "lineno", 1) - 1].strip() if self.src_lines else ""
        raise TranspileError(f"{self.fn_name}, line {ln}: {msg}\n    {src}")


# --------------------------------------------------------------------------------------
# expressions
# --------------------------------------------------------------------------------------

def _expr(c: _Ctx, n, defined: set):
    """Return (cpp_code, FType)."""
    if isinstance(n, ast.Constant):
        v = n.value
        if isinstance(v, bool):
            return ("true" if v else "false"), BOOL
        if isinstance(v, int):
            try:
                return _ilit(v), I64
            except ValueError:
                c.err(n, f"integer literal {v} does not fit in int64")
        if isinstance(v, float):
            return _flit(v), F64
        if isinstance(v, str):
            from .codegen import _cpp_string_literal
            return f"std::string({_cpp_string_literal(v)}, {len(v.encode('utf-8'))})", STR
        c.err(n, f"unsupported constant {v!r}")

    if isinstance(n, ast.Name):
        if n.id in c.env:
            if n.id not in defined:
                c.err(n, f"variable {n.id!r} may be used before it is assigned")
            t = c.env[n.id]
            return n.id, t
        if n.id in ("True", "False"):
            return n.id.lower(), BOOL
        if n.id in c.globals:
            return _global_constant(c, n, n.id, c.globals[n.id])
        c.err(n, f"unknown name {n.id!r}")

    if isinstance(n, ast.UnaryOp):
        if isinstance(n.op, ast.USub) and isinstance(n.operand, ast.Constant) and \
                isinstance(n.operand.value, (int, float)) and not isinstance(n.operand.value, bool):
            v = -n.operand.value
            if isinstance(v, int):
                try:
                    return _ilit(v), I64
                except ValueError:
                    c.err(n, f"integer literal {v} does not fit in int64")
            return _flit(v), F64
        a, t = _expr(c, n.operand, defined)
        if isinstance(n.op, ast.Not):
            if t != BOOL:
                c.err(n, "`not` needs a bool (fleetpy does not use truthiness)")
            return f"(not {a})", BOOL
        if isinstance(n.op, ast.USub):
            if _is_int(t):
                return f"fp::neg({a})", I64
            if _is_float(t):
                return f"(-{a})", F64
            c.err(n, f"unary - on {t.key}")
        if isinstance(n.op, ast.UAdd):
            if _is_int(t):
                return f"int64_t({a})", I64
            if _is_float(t):
                return f"double({a})", F64
            c.err(n, f"unary + on {t.key}")
        if isinstance(n.op, ast.Invert):
            if _is_int(t):
                return f"(~int64_t({a}))", I64  # ~x == -x-1 in Python and in two's complement
            c.err(n, f"~ on {t.key}")
        c.err(n, "unsupported unary operator")

    if isinstance(n, ast.BinOp):
        a, ta = _expr(c, n.left, defined)
        b, tb = _expr(c, n.right, defined)
        op = n.op
        if ta == BOOL or tb == BOOL:
            c.err(n, "arithmetic on bool is not allowed; convert with int(...) explicitly")
        if isinstance(op, ast.Add) and ta in (STR, CHAR) and tb in (STR, CHAR):
            return f"(fp::str({a}) + fp::str({b}))", STR
        if isinstance(op, (ast.BitAnd, ast.BitOr, ast.BitXor, ast.LShift, ast.RShift)):
            if not (_is_int(ta) and _is_int(tb)):
                c.err(n, f"bitwise {type(op).__name__} needs ints, got {ta.key} and {tb.key}")
            if isinstance(op, ast.LShift):
                return f"fp::shl(int64_t({a}), int64_t({b}))", I64
            if isinstance(op, ast.RShift):
                return f"fp::shr(int64_t({a}), int64_t({b}))", I64
            sym = {ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^"}[type(op)]
            return f"(int64_t({a}) {sym} int64_t({b}))", I64
        if isinstance(op, ast.Add) and isinstance(ta, T.ListOf) and ta == tb:
            return f"fp::concat({a}, {b})", ta
        if not (_is_num(ta) and _is_num(tb)):
            c.err(n, f"operator {type(op).__name__} on {ta.key} and {tb.key}")
        both_int = _is_int(ta) and _is_int(tb)
        if isinstance(op, ast.Div):
            if both_int:
                return f"fp::idiv(int64_t({a}), int64_t({b}))", F64
            return f"fp::fdiv(double({a}), double({b}))", F64
        if isinstance(op, (ast.FloorDiv, ast.Mod)):
            if not both_int:
                c.err(n, "// and % are only supported on ints (float floor-division semantics differ); "
                         "use math.floor(a / b)")
            fn = "floordiv" if isinstance(op, ast.FloorDiv) else "mod"
            return f"fp::{fn}(int64_t({a}), int64_t({b}))", I64
        if isinstance(op, ast.Pow):
            if not (isinstance(n.right, ast.Constant) and isinstance(n.right.value, int)
                    and not isinstance(n.right.value, bool) and 0 <= n.right.value <= 64):
                c.err(n, "** needs a non-negative int literal exponent (<= 64); use math.pow for floats")
            if both_int:
                return f"fp::ipow(int64_t({a}), {n.right.value})", I64
            return f"fp::fpow(double({a}), {float(n.right.value)!r})", F64
        fn = {ast.Add: "add", ast.Sub: "sub", ast.Mult: "mul"}.get(type(op))
        if fn is None:
            c.err(n, f"unsupported operator {type(op).__name__}")
        if both_int:
            return f"fp::{fn}(int64_t({a}), int64_t({b}))", I64
        sym = {"add": "+", "sub": "-", "mul": "*"}[fn]
        return f"(double({a}) {sym} double({b}))", F64

    if isinstance(n, ast.BoolOp):
        parts = []
        for v in n.values:
            s, t = _expr(c, v, defined)
            if t != BOOL:
                c.err(n, "`and`/`or` only on bools (Python returns an operand otherwise)")
            parts.append(s)
        j = " and " if isinstance(n.op, ast.And) else " or "
        return "(" + j.join(parts) + ")", BOOL

    if isinstance(n, ast.Compare):
        terms = [n.left] + list(n.comparators)
        out = []
        codes = [_expr(c, t, defined) for t in terms]
        for (op, (a, ta), (b, tb)) in zip(n.ops, codes[:-1], codes[1:]):
            if isinstance(op, (ast.In, ast.NotIn, ast.Is, ast.IsNot)):
                c.err(n, "in / is are not supported")
            sym = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">="}[type(op)]
            if {ta.key, tb.key} == {"char", "str"}:
                # a fl.char is a one-character str in Python: compare as strings
                a, b, ta = f"fp::str({a})", f"fp::str({b})", STR
            elif _is_num(ta) and _is_num(tb):
                if _is_int(ta) != _is_int(tb):
                    c.err(n, "comparing an int with a float: Python compares exactly; convert explicitly "
                             "with float(...)")
                if _is_int(ta):
                    a, b = f"int64_t({a})", f"int64_t({b})"
                else:
                    a, b = f"double({a})", f"double({b})"
            elif ta != tb:
                c.err(n, f"comparing {ta.key} with {tb.key}")
            elif sym not in ("==", "!=") and not (ta in (STR, BOOL, CHAR)):
                c.err(n, f"ordering comparison on {ta.key} is not supported")
            out.append(f"({a} {sym} {b})")
        return "(" + " and ".join(out) + ")", BOOL

    if isinstance(n, ast.IfExp):
        cond, tc = _expr(c, n.test, defined)
        if tc != BOOL:
            c.err(n, "condition must be a bool")
        a, ta = _expr(c, n.body, defined)
        b, tb = _expr(c, n.orelse, defined)
        if _is_num(ta) and _is_num(tb) and _is_int(ta) == _is_int(tb):
            t = I64 if _is_int(ta) else F64
            cast = "int64_t" if t == I64 else "double"
            return f"({cond} ? {cast}({a}) : {cast}({b}))", t
        if ta != tb:
            c.err(n, f"the two branches have different types ({ta.key}, {tb.key})")
        return f"({cond} ? {a} : {b})", ta

    if isinstance(n, ast.Attribute):
        if isinstance(n.value, ast.Name) and n.value.id == "math":
            if n.attr in _MATHCONST:
                return _flit(_MATHCONST[n.attr]), F64
            c.err(n, f"math.{n.attr} is not supported as a value")
        base, tb = _expr(c, n.value, defined)
        if isinstance(tb, T.StructType):
            for fname, ft in tb.fields:
                if fname == n.attr:
                    return f"{base}.{fname}", ft
            c.err(n, f"{tb.name} has no field {n.attr!r}")
        if isinstance(tb, _DatumType):
            if n.attr in tb.fields:
                return f"{base}.{n.attr}", tb.fields[n.attr]
            c.err(n, f"a datum has no field {n.attr!r}")
        c.err(n, f"attribute access on {tb.key}")

    if isinstance(n, ast.Subscript):
        base, tb = _expr(c, n.value, defined)
        if isinstance(tb, T.TupleOf):
            if not (isinstance(n.slice, ast.Constant) and isinstance(n.slice.value, int)):
                c.err(n, "tuple index must be an int literal")
            i = n.slice.value
            if not (-len(tb.elems) <= i < len(tb.elems)):
                c.err(n, "tuple index out of range")
            i %= len(tb.elems)
            return f"std::get<{i}>({base})", tb.elems[i]
        if isinstance(n.slice, ast.Slice):
            sl = n.slice
            if sl.step is not None:
                c.err(n, "slices with a step are not supported")
            parts = []
            for b_ in (sl.lower, sl.upper):
                if b_ is None:
                    parts += ["false", "int64_t(0)"]
                else:
                    bc, bt = _expr(c, b_, defined)
                    if not _is_int(bt):
                        c.err(n, "slice bounds must be ints")
                    parts += ["true", f"int64_t({bc})"]
            if tb == STR:
                return f"fp::cp_slice({base}, {', '.join(parts)})", STR
            if isinstance(tb, T.ListOf):
                return f"fp::slice({base}, {', '.join(parts)})", tb
            c.err(n, f"slicing {tb.key}")
        idx, ti = _expr(c, n.slice, defined)
        if not _is_int(ti):
            c.err(n, "index must be an int")
        if isinstance(tb, T.ListOf):
            return f"fp::index({base}, int64_t({idx}))", tb.elem
        if isinstance(tb, _DataType):
            return f"fp::index({base}, int64_t({idx}))", tb.datum
        if tb == STR:
            return f"fp::cp_index({base}, int64_t({idx}))", STR
        c.err(n, f"indexing {tb.key}")

    if isinstance(n, ast.Call):
        return _call(c, n, defined)

    if isinstance(n, (ast.List, ast.Tuple)):
        if not n.elts:
            c.err(n, "empty literals need a declared type: write `xs: list[int] = []`")
        items = [_expr(c, e, defined) for e in n.elts]
        if isinstance(n, ast.Tuple):
            t = T.TupleOf([ti for _, ti in items])
            return f"{t.cpp}{{" + ", ".join(s for s, _ in items) + "}", t
        t0 = items[0][1]
        if any(ti != t0 for _, ti in items):
            c.err(n, "list elements must all have the same type")
        t = T.ListOf(t0)
        return f"{t.cpp}{{" + ", ".join(s for s, _ in items) + "}", t

    c.err(n, f"unsupported expression ({type(n).__name__})")


def _call(c: _Ctx, n: ast.Call, defined):
    if n.keywords:
        c.err(n, "keyword arguments are not supported")
    args = [_expr(c, a, defined) for a in n.args]
    f = n.func
    # self.grammar_prior() / recursion_count() / nodes() inside a prior
    if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "self" \
            and c.kind == "prior":
        if args:
            c.err(n, f"self.{f.attr}() takes no arguments")
        if f.attr == "grammar_prior":
            return "this->Super::compute_prior()", F64
        if f.attr == "recursion_count":
            return "int64_t(this->recursion_count())", I64
        if f.attr == "nodes":
            return "int64_t(this->value.count())", I64
        c.err(n, "a prior may call self.grammar_prior(), self.recursion_count() and self.nodes()")
    # int methods
    if isinstance(f, ast.Attribute) and f.attr in ("bit_count", "bit_length"):
        base, tb = _expr(c, f.value, defined)
        if not _is_int(tb) or args:
            c.err(n, f".{f.attr}() is an int method with no arguments")
        return f"fp::{f.attr}(int64_t({base}))", I64
    # self.call(x) / self.logprob(x, y) inside a likelihood
    if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "self":
        if c.kind != "likelihood" or f.attr not in ("call", "logprob"):
            c.err(n, "only self.call(x) (deterministic) and self.logprob(x, y) (stochastic) are "
                     "supported on self, inside a likelihood")
        if f.attr == "call":
            if c.stochastic:
                c.err(n, "a stochastic hypothesis outputs a distribution; use self.logprob(x, y)")
            if len(args) not in (1, 2):
                c.err(n, "self.call takes (x) or (x, default)")
            a, ta = args[0]
            if ta != c.grammar.input:
                a = _coerce(c, n, a, ta, c.grammar.input)
            if len(args) == 1:
                return f"fp_callx({a})", c.grammar.output
            d = _coerce(c, n, args[1][0], args[1][1], c.grammar.output)
            # undefined -> default (Fleet's call(x, err) returns err on error)
            return (f"([&]() {{ auto [fp_o, fp_v] = this->fp_call({a}); "
                    f"return fp_o ? fp_v : {c.grammar.output.cpp}({d}); }}())"), c.grammar.output
        if not c.stochastic:
            c.err(n, "self.logprob is for stochastic hypotheses; use self.call(x)")
        if len(args) != 2:
            c.err(n, "self.logprob takes (x, y)")
        (a, ta), (b, tb) = args
        if ta != c.grammar.input or tb != c.grammar.output:
            c.err(n, f"self.logprob expects ({c.grammar.input.key}, {c.grammar.output.key})")
        return f"this->fp_logprob({a}, {b})", F64
    if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.value.id == "math":
        name = f.attr
        if name in _MATH1:
            if len(args) != 1 or not _is_num(args[0][1]):
                c.err(n, f"math.{name} takes one number")
            a = args[0][0]
            return f"fp::mcheck({_MATH1[name]}(double({a})), double({a}))", F64
        if name in _UNSUPPORTED_MATH:
            c.err(n, f"math.{name}: {_UNSUPPORTED_MATH[name]}")
        if name in _MATH2:
            if len(args) != 2 or not all(_is_num(t) for _, t in args):
                c.err(n, f"math.{name} takes two numbers")
            (a, _), (b, _) = args
            if name == "pow":
                return f"fp::fpow(double({a}), double({b}))", F64
            return f"{_MATH2[name]}(double({a}), double({b}))", F64
        if name in ("floor", "ceil", "trunc"):
            if len(args) != 1:
                c.err(n, f"math.{name} takes one argument")
            a, t = args[0]
            if _is_int(t):
                return f"int64_t({a})", I64
            if _is_float(t):
                return f"fp::to_int(std::{name}(double({a})))", I64
            c.err(n, f"math.{name} on {t.key}")
        if name in ("isnan", "isinf", "isfinite"):
            if len(args) != 1 or not _is_num(args[0][1]):
                c.err(n, f"math.{name} takes one number")
            return f"std::{name}(double({args[0][0]}))", BOOL
        c.err(n, f"math.{name} is not supported")
    if not isinstance(f, ast.Name):
        c.err(n, "only calls to builtins, math functions, structs and other tier-2 primitives are supported")
    name = f.id
    if name == "str":
        if len(args) != 1 or args[0][1] not in (STR, CHAR):
            c.err(n, "str() is only supported on str and fl.char")
        return f"fp::str({args[0][0]})", STR
    if name == "len":
        if len(args) != 1:
            c.err(n, "len takes one argument")
        a, t = args[0]
        if isinstance(t, _DataType):
            return f"int64_t(({a}).size())", I64
        if t == STR:
            return f"int64_t(fp::cp_len({a}))", I64
        if isinstance(t, (T.ListOf, T.SetOf)):
            return f"int64_t(({a}).size())", I64
        c.err(n, f"len of {t.key}")
    if name == "abs":
        if len(args) != 1:
            c.err(n, "abs takes one argument")
        a, t = args[0]
        if _is_int(t):
            return f"fp::iabs(int64_t({a}))", I64
        if _is_float(t):
            return f"std::fabs(double({a}))", F64
        c.err(n, f"abs of {t.key}")
    if name in ("min", "max"):
        if len(args) != 2:
            c.err(n, f"{name} takes exactly two arguments")
        (a, ta), (b, tb) = args
        if _is_num(ta) and _is_num(tb):
            if _is_int(ta) != _is_int(tb):
                c.err(n, f"{name} of an int and a float: convert explicitly")
            if _is_float(ta):
                # Python's min/max use < and > and keep the first on ties; with NaN the result
                # depends on argument order exactly as in fp::minv/maxv.
                return f"fp::{name}v(double({a}), double({b}))", F64
            return f"fp::{name}v(int64_t({a}), int64_t({b}))", I64
        if ta != tb or ta not in (STR,):
            c.err(n, f"{name} on {ta.key}, {tb.key}")
        return f"fp::{name}v({a}, {b})", ta
    if name == "float":
        if len(args) != 1 or not _is_num(args[0][1]):
            c.err(n, "float() takes one number")
        return f"double({args[0][0]})", F64
    if name == "int":
        if len(args) != 1:
            c.err(n, "int() takes one argument")
        a, t = args[0]
        if t == BOOL:
            return f"int64_t({a} ? 1 : 0)", I64
        if _is_int(t):
            return f"int64_t({a})", I64
        if _is_float(t):
            return f"fp::to_int(double({a}))", I64
        c.err(n, f"int() of {t.key}")
    if name == "bool":
        c.err(n, "bool() uses truthiness; write an explicit comparison")
    if name in ("Undefined",):
        c.err(n, "Undefined can only be raised")
    if name not in c.structs:
        cls = c.globals.get(name)
        ft = getattr(cls, "__fleet_type__", None) if isinstance(cls, type) else None
        if isinstance(ft, T.StructType):
            c.structs[name] = ft
            c.extra_structs.append(ft)
    if name in c.structs:
        st = c.structs[name]
        if len(args) != len(st.fields):
            c.err(n, f"{name} takes {len(st.fields)} arguments")
        parts = [_coerce(c, n, a, ta, ft) for (a, ta), (_, ft) in zip(args, st.fields)]
        return f"{name}(" + ", ".join(parts) + ")", st
    if name in c.prims:
        p = c.prims[name]
        if len(args) != p.arity:
            c.err(n, f"{name} takes {p.arity} arguments")
        parts = [_coerce(c, n, a, ta, pt) for (a, ta), pt in zip(args, p.argtypes)]
        return f"{name}(" + ", ".join(parts) + ")", p.rettype
    c.err(n, f"unknown function {name!r} (only tier-2 primitives can be called; tier-1 bodies "
             f"have no Python version to test against)")


def _coerce(c, node, code, have: T.FType, want: T.FType) -> str:
    """Convert an expression of type ``have`` to ``want`` with Python-compatible checks."""
    if have == want:
        return code
    if _is_int(want) and _is_int(have):
        base = _scalar(want)
        v = f"fp::narrow<{base.cpp}>(int64_t({code}))"
        return f"{want.cpp}({v})" if isinstance(want, T.NewType) else v
    if _is_float(want) and _is_float(have):
        return f"{want.cpp}(double({code}))"
    if _is_float(want) and _is_int(have):
        # Python converts with float(int): round-to-nearest, the same as (double)int64_t
        return f"{want.cpp}(double(int64_t({code})))"
    if want == STR and have == CHAR:
        return f"fp::str({code})"
    if isinstance(want, T.NewType) and _scalar(want) == have:
        return f"{want.cpp}({code})"
    if isinstance(have, T.NewType) and _scalar(have) == want:
        return f"{_scalar(have).cpp}({code})"
    c.err(node, f"expected {want.key}, got {have.key} (no implicit conversion)")


class _DataType(T.FType):
    """The `data` argument of a whole-dataset likelihood (Fleet's std::span of datums)."""

    def __init__(self, g):
        self.cpp = "data_t"
        self.key = "data"
        self.datum = _DatumType(g)


class _DatumType(T.FType):
    def __init__(self, g):
        self.cpp = "datum_t"
        self.key = "datum"
        self.fields = {"input": g.input, "output": g.output, "reliability": F64, "count": F64}


# --------------------------------------------------------------------------------------
# statements
# --------------------------------------------------------------------------------------

def _infer_locals(c: _Ctx, body):
    """First pass: give every local a type from its first assignment (source order)."""
    for st in ast.walk(ast.Module(body=body, type_ignores=[])):
        if isinstance(st, ast.AnnAssign):
            if not isinstance(st.target, ast.Name):
                c.err(st, "only simple names can be assigned")
            nm = st.target.id
            try:
                t = T.resolve(eval(compile(ast.Expression(st.annotation), "<ann>", "eval"),
                                   _ann_namespace(c)))
            except Exception as e:  # noqa: BLE001
                c.err(st, f"cannot resolve annotation: {e}")
            _declare(c, st, nm, t)


def _ann_namespace(c):
    import fleetpy
    ns = dict(c.globals)
    ns.update({"int": int, "float": float, "bool": bool, "str": str, "list": list, "tuple": tuple,
          "frozenset": frozenset, "fl": fleetpy, "fleetpy": fleetpy})
    for nm, st in c.structs.items():
        ns[nm] = st.pycls
    for k in ("i8", "i16", "i32", "i64", "f64"):
        ns[k] = getattr(T, k)
    return ns


def _declare(c, node, name, t):
    if name in c.params:
        c.err(node, f"assigning to parameter {name!r} is not allowed")
    if name in ("math", "self", "di", "data", "fp", "len", "abs", "min", "max", "int", "float", "str"):
        c.err(node, f"{name!r} is reserved")
    T.check_identifier(name, "local variable")
    if name in c.locals and c.locals[name] != t:
        c.err(node, f"{name!r} was {c.locals[name].key}, cannot become {t.key}")
    c.locals[name] = t
    c.env[name] = t


def _assign_type(c, node, name, t):
    """Type of a local assigned from an expression of type t (ints widen to i64, floats to f64)."""
    if name in c.locals:
        return c.locals[name]
    if _is_int(t) and not isinstance(t, T.NewType):
        t = I64
    _declare(c, node, name, t)
    return t


def _stmts(c: _Ctx, body, defined: set, ind: int):
    """Compile statements. Returns (lines, defined_after, always_returns)."""
    out = []
    pad = "\t" * ind
    defined = set(defined)
    for st in body:
        if isinstance(st, ast.Return):
            if st.value is None:
                c.err(st, "return needs a value")
            code, t = _expr(c, st.value, defined)
            out.append(f"{pad}return {_coerce(c, st, code, t, c.ret)};")
            return out, defined, True
        if isinstance(st, ast.Raise):
            ex = st.exc
            nm = None
            if isinstance(ex, ast.Call):
                ex = ex.func
            if isinstance(ex, ast.Name):
                nm = ex.id
            elif isinstance(ex, ast.Attribute):
                nm = ex.attr
            if nm != "Undefined":
                c.err(st, "only `raise Undefined` (fleetpy.Undefined) is supported")
            out.append(f"{pad}fp::undefined();")
            return out, defined, True
        if isinstance(st, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
            if isinstance(st, ast.Assign):
                if len(st.targets) != 1 or not isinstance(st.targets[0], ast.Name):
                    c.err(st, "only `name = expr` assignments are supported")
                nm, val = st.targets[0].id, st.value
            elif isinstance(st, ast.AnnAssign):
                if st.value is None:
                    c.err(st, "annotated declarations need a value")
                nm, val = st.target.id, st.value
            else:
                if not isinstance(st.target, ast.Name):
                    c.err(st, "only names can be augmented")
                nm = st.target.id
                if nm in c.params:
                    c.err(st, f"assigning to parameter {nm!r} is not allowed")
                val = ast.BinOp(left=ast.Name(id=nm, ctx=ast.Load(), lineno=st.lineno),
                                op=st.op, right=st.value, lineno=st.lineno)
            if isinstance(st, ast.AnnAssign) and isinstance(val, (ast.List,)) and not val.elts:
                t = c.locals[nm]
                out.append(f"{pad}{nm} = {t.cpp}{{}};")
                defined.add(nm)
                continue
            code, t = _expr(c, val, defined)
            lt = _assign_type(c, st, nm, t)
            out.append(f"{pad}{nm} = {_coerce(c, st, code, t, lt)};")
            defined.add(nm)
            continue
        if isinstance(st, ast.If):
            cond, tc = _expr(c, st.test, defined)
            if tc != BOOL:
                c.err(st, "if-condition must be a bool (no truthiness)")
            a, da, ra = _stmts(c, st.body, defined, ind + 1)
            b, db, rb = _stmts(c, st.orelse, defined, ind + 1) if st.orelse else ([], set(defined), False)
            out.append(f"{pad}if({cond}) {{")
            out += a
            out.append(f"{pad}}}")
            if st.orelse:
                out.append(f"{pad}else {{")
                out += b
                out.append(f"{pad}}}")
            if ra and rb:
                return out, defined, True
            if ra:
                defined = db
            elif rb:
                defined = da
            else:
                defined = da & db
            continue
        if isinstance(st, ast.For):
            if st.orelse:
                c.err(st, "for/else is not supported")
            if not isinstance(st.target, ast.Name):
                c.err(st, "loop variable must be a name")
            it = st.iter
            if not (isinstance(it, ast.Call) and isinstance(it.func, ast.Name) and it.func.id == "range"
                    and 1 <= len(it.args) <= 3 and not it.keywords):
                c.err(st, "only `for i in range(...)` loops are supported")
            rargs = []
            for a in it.args:
                s, t = _expr(c, a, defined)
                if not _is_int(t):
                    c.err(st, "range() arguments must be ints")
                rargs.append(f"int64_t({s})")
            if len(rargs) == 1:
                rargs = ["int64_t(0)"] + rargs + ["int64_t(1)"]
            elif len(rargs) == 2:
                rargs.append("int64_t(1)")
            v = st.target.id
            if v in c.locals and c.locals[v] != I64:
                c.err(st, f"{v!r} is not an int")
            _declare(c, st, v, I64)
            k = c.tmp
            c.tmp += 1
            lo, hi, step = f"fp_lo{k}", f"fp_hi{k}", f"fp_st{k}"
            out.append(f"{pad}{{ const int64_t {lo} = {rargs[0]}, {hi} = {rargs[1]}, {step} = {rargs[2]};")
            out.append(f"{pad}  if({step} == 0) fp::undefined(); // range() arg 3 must not be zero")
            # iterate with an index count so that no int64 overflow can occur in the loop variable
            out.append(f"{pad}  for(int64_t fp_i{k} = 0; ; fp_i{k}++) {{")
            out.append(f"{pad}\t__int128 fp_v{k} = (__int128){lo} + (__int128){step} * fp_i{k};")
            out.append(f"{pad}\tif({step} > 0 ? fp_v{k} >= {hi} : fp_v{k} <= {hi}) break;")
            out.append(f"{pad}\tif(++fp_iters > {c.max_iters}LL) fp::undefined(); // iteration cap")
            out.append(f"{pad}\t{v} = (int64_t)fp_v{k};")
            inner, _, _ = _stmts(c, st.body, defined | {v}, ind + 1)
            out += inner
            out.append(f"{pad}  }}")
            out.append(f"{pad}}}")
            # a loop may run zero times: nothing assigned inside it (including v) is definitely assigned
            continue
        if isinstance(st, ast.Pass):
            continue
        if isinstance(st, (ast.Break, ast.Continue)):
            out.append(pad + ("break;" if isinstance(st, ast.Break) else "continue;"))
            return out, defined, False
        if isinstance(st, ast.Expr) and isinstance(st.value, ast.Constant) and isinstance(st.value.value, str):
            continue  # docstring
        if isinstance(st, ast.Expr) and isinstance(st.value, ast.Constant) and st.value.value is Ellipsis:
            c.err(st, "a body of `...` has no implementation: give cpp= (tier 1) or write the body")
        c.err(st, f"unsupported statement ({type(st).__name__})")
    return out, defined, False


def _function_ast(fn):
    try:
        src = textwrap.dedent(inspect.getsource(fn))
    except (OSError, TypeError) as e:
        raise TranspileError(f"cannot read the source of {fn.__qualname__} ({e}); tier-2 primitives "
                             f"must be defined in a file") from None
    tree = ast.parse(src)
    fdefs = [n for n in tree.body if isinstance(n, ast.FunctionDef)]
    if len(fdefs) != 1:
        raise TranspileError(f"expected one function definition for {fn.__qualname__}")
    line0 = fn.__code__.co_firstlineno
    # decorators precede `def`; co_firstlineno points at the first decorator
    return fdefs[0], src.splitlines(), line0


def _check_closure(fn):
    """Reject functions that capture or depend on mutable state outside their arguments."""
    cv = inspect.getclosurevars(fn)
    if cv.nonlocals:
        raise TranspileError(f"{fn.__qualname__} closes over {sorted(cv.nonlocals)}; tier-2 bodies must be pure")


def _global_constant(c, node, name, v):
    """A module-level constant, frozen into the generated C++ at build time (the generated
    source is part of the build hash, so changing the constant rebuilds)."""
    if isinstance(v, bool):
        return ("true" if v else "false"), BOOL
    if isinstance(v, int):
        try:
            return _ilit(v), I64
        except ValueError:
            c.err(node, f"constant {name} = {v} does not fit in int64")
    if isinstance(v, float):
        return _flit(v), F64
    if isinstance(v, str):
        from .codegen import _cpp_string_literal
        return f"std::string({_cpp_string_literal(v)}, {len(v.encode('utf-8'))})", STR
    if isinstance(v, (list, tuple)) and v:
        if name not in c.statics:
            items = [_global_constant(c, node, f"{name}[{i}]", x) for i, x in enumerate(v)]
            ts = {t.key for _, t in items}
            if ts == {"i64", "f64"}:
                c.err(node, f"constant {name} mixes ints and floats; write them all as floats")
            if len(ts) != 1 or items[0][1].key not in ("i64", "f64", "bool", "str"):
                c.err(node, f"constant {name} must be a list or tuple of numbers, bools or strings")
            t = T.ListOf(items[0][1])
            sname = f"fp_k_{name}"
            c.statics[name] = (sname, t, f"static const {t.cpp} {sname} = {{ " + ", ".join(x for x, _ in items) + " };")
        sname, t, _ = c.statics[name]
        return sname, t
    c.err(node, f"{name!r} is not a constant fleetpy can use (numbers, bools, strings, or lists of them)")


def _body_with_prelude(lines, always_returns, locals_, statics=None):
    decl = [f"\t{d}" for _, _, d in (statics or {}).values()]
    decl += [f"\t{t.cpp} {n}{{}};" for n, t in locals_.items()]
    if any("fp_iters" in ln for ln in lines):
        decl.append("\tint64_t fp_iters = 0;")
    tail = [] if always_returns else ["\tfp::undefined(); // fell off the end (Python returns None)"]
    return "\n".join(decl + lines + tail)


def transpile_primitive(p, grammar=None) -> str:
    """Return the C++ body of tier-2 primitive ``p``."""
    from .grammar import Primitive  # noqa: F401
    fdef, lines, line0 = _function_ast(p.fn)
    _check_closure(p.fn)
    g = _owning_grammar(p)
    prims = {r.name: r for r in g.rules if getattr(r, "tier", None) == 2} if g else {}
    structs = {s.name: s for s in (g.structs() if g else []) if isinstance(s, T.StructType)}
    env = dict(zip(p.argnames, p.argtypes))
    c = _Ctx(p.name, lines, line0, env, p.rettype, prims, structs)
    c.globals = p.fn.__globals__
    c.max_iters = p.max_iters
    _infer_locals(c, fdef.body)
    body, _, always = _stmts(c, fdef.body, set(env), 1)
    p.extra_structs = list(c.extra_structs)
    for t in c.locals.values():
        p.extra_structs += [s for s in t.structs() if isinstance(s, T.StructType)]
    return _body_with_prelude(body, always, c.locals, c.statics)


def _owning_grammar(p):
    return getattr(p, "grammar", None)


# --------------------------------------------------------------------------------------
# tier-2 likelihoods
# --------------------------------------------------------------------------------------

class _PendingLikelihood:
    """Placeholder set by @fl.likelihood; becomes a Likelihood when the hypothesis class is built."""

    def __init__(self, fn, nonpositive, total=False):
        self.fn, self.nonpositive, self.total = fn, nonpositive, total


def likelihood(fn=None, *, nonpositive: bool = False, total: bool = False):
    """Decorator for a tier-2 likelihood method.

    Per datum (default):  ``def f(self, di) -> float``.
    Whole dataset (total=True): ``def f(self, data) -> float``, with len(data) and data[i];
    this replaces Fleet's compute_likelihood (as Models/Number-Simple does).

    ``self.call(x)`` runs a deterministic hypothesis; if the program is undefined on x, the
    datum's (or with total=True the dataset's) log likelihood is -inf -- an uncaught Undefined
    anywhere in the body means the same. ``self.call(x, default)`` returns ``default`` instead
    (Fleet's call(x, err)). ``self.logprob(x, y)`` is log P(y | x) for a stochastic hypothesis.
    """
    def deco(f):
        return _PendingLikelihood(f, nonpositive, total)
    return deco(fn) if fn is not None else deco


class _PendingPrior:
    def __init__(self, fn):
        self.fn = fn


def prior(fn):
    """Decorator for a tier-2 prior method ``def p(self) -> float``, replacing compute_prior.

    It may call self.grammar_prior() (Fleet's PCFG prior: the default), self.recursion_count()
    and self.nodes(). As in Fleet, a program with more than MAX_NODES nodes still gets -inf
    before this runs.
    """
    return _PendingPrior(fn)


def transpile_prior(pp: _PendingPrior, grammar) -> str:
    fdef, lines, line0 = _function_ast(pp.fn)
    _check_closure(pp.fn)
    if [a.arg for a in fdef.args.args] != ["self"]:
        raise TranspileError("a prior must be `def name(self) -> float`")
    structs = {s.name: s for s in grammar.structs() if isinstance(s, T.StructType)}
    c = _Ctx(pp.fn.__name__, lines, line0, {}, F64, {}, structs, kind="prior", grammar=grammar)
    c.globals = pp.fn.__globals__
    _infer_locals(c, fdef.body)
    body, _, always = _stmts(c, fdef.body, set(), 1)
    inner = _body_with_prelude(body, always, c.locals, c.statics)
    return f"try {{\n{inner}\n}} catch (VMSRuntimeError&) {{\n\treturn -infinity;\n}}"


def transpile_likelihood(pl: _PendingLikelihood, grammar, H=None):
    from .likelihoods import Likelihood
    fdef, lines, line0 = _function_ast(pl.fn)
    _check_closure(pl.fn)
    params = [a.arg for a in fdef.args.args]
    want = ["self", "data"] if pl.total else ["self", "di"]
    if params != want or fdef.args.vararg or fdef.args.kwonlyargs:
        raise TranspileError(f"a likelihood must be `def name({', '.join(want)}) -> float`")
    env = {"data": _DataType(grammar)} if pl.total else {"di": _DatumType(grammar)}
    structs = {s.name: s for s in grammar.structs() if isinstance(s, T.StructType)}
    prims = {r.name: r for r in grammar.rules if getattr(r, "tier", None) == 2}
    c = _Ctx(pl.fn.__name__, lines, line0, env, F64, prims, structs, kind="likelihood", grammar=grammar)
    c.globals = pl.fn.__globals__
    c.stochastic = bool(H is not None and H.stochastic)
    _infer_locals(c, fdef.body)
    body, _, always = _stmts(c, fdef.body, set(env), 2)
    inner = _body_with_prelude(body, always, c.locals, c.statics)
    OUT = grammar.output.cpp
    IN = grammar.input.cpp
    callx = "" if c.stochastic else (
        f"auto fp_callx = [this](const {IN}& fp_x) -> {OUT} {{\n"
        f"\tauto [fp_ok, fp_out] = this->fp_call(fp_x);\n"
        f"\tif(not fp_ok) fp::undefined();\n"
        f"\treturn fp_out;\n"
        f"}};\n")
    code = callx + f"try {{\n{inner}\n}} catch (VMSRuntimeError&) {{\n\treturn -infinity;\n}}"
    lk = Likelihood(code, pl.nonpositive, "transpiled:" + pl.fn.__qualname__,
                    needs="dist" if c.stochastic else "value")
    lk.extra_structs = list(c.extra_structs)
    lk.total = pl.total
    lk.pyfn = pl.fn
    lk.source = "\n".join(lines)
    return lk
