"""
The fleetpy type system.

Every Python annotation in a model maps to exactly one ``FType``, which is fixed at build
time. An FType owns *everything* fleetpy needs to know about its values, so adding a type
means writing one class:

  cpp, key          the C++ spelling, and a canonical string (hashing, deduplication)
  category          what kind of value it is inside transpiled code: "bool", "int", "float",
                    "str", "char", "list", "tuple", "set", "struct" or "opaque"
  children()        the component types (list element, struct fields, ...)
  headers()         C++ headers this type itself needs (not its children's)
  to_cpp(v, mod)    validate a Python value and convert it to what the compiled module accepts
  from_cpp(v)       convert a value returned by the compiled module back to Python
  literal(v)        a C++ expression for the Python value v (validates v)
  fits(v)           does a Python value of this type's category lie in the type's range?
                    (the checks compiled code applies when it narrows a value to this type)
  sample(rng, edge) a random value, biased towards edge cases when ``edge`` (for testing)

Fleet keys grammar nonterminals by C++ type, so two annotations with the same C++ type are
the *same* nonterminal. ``newtype`` makes distinct roles distinct.
"""

from __future__ import annotations

import dataclasses
import keyword
import math
import re
import typing

CPP_KEYWORDS = frozenset("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
co_return co_yield decltype default delete do double dynamic_cast else enum explicit export
extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast requires return
short signed sizeof static static_assert static_cast struct switch template this thread_local
throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while
xor xor_eq
""".split())

_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def check_identifier(name: str, what: str) -> str:
    """Raise unless ``name`` can be used verbatim as a C++ (and Python) identifier."""
    if not isinstance(name, str) or not _IDENT.match(name):
        raise ValueError(f"{what} {name!r} is not a valid identifier")
    if name in CPP_KEYWORDS or keyword.iskeyword(name):
        raise ValueError(f"{what} {name!r} is a C++ or Python keyword; rename it")
    if name.startswith("fp_") or name.startswith("__"):
        raise ValueError(f"{what} {name!r} uses a prefix reserved for generated code (fp_ or __)")
    return name


def cpp_string_literal(s: str) -> str:
    """A C++ string literal ("...") holding exactly the UTF-8 bytes of ``s``."""
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:   # octal escapes: unambiguous, unlike \x which is greedy
            out.extend("\\%03o" % b for b in ch.encode("utf-8"))
    return '"' + "".join(out) + '"'


def cpp_string(s: str) -> str:
    """A C++ std::string expression for ``s`` (explicit length, so embedded NULs survive)."""
    return f"std::string({cpp_string_literal(s)}, {len(s.encode('utf-8'))})"


def cpp_double(v: float) -> str:
    """An exact C++ double expression (hex float literal)."""
    if math.isnan(v):
        return "std::numeric_limits<double>::quiet_NaN()"
    if math.isinf(v):
        return ("" if v > 0 else "-") + "std::numeric_limits<double>::infinity()"
    return f"double({float.hex(v)})"


def is_int_value(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)   # bool is an int subclass


class FType:
    """Base class of all fleetpy types."""

    cpp: str
    key: str
    category: str

    def children(self) -> tuple:
        return ()

    def headers(self) -> list:
        return []

    def to_cpp(self, v, mod):  # pragma: no cover - abstract
        raise NotImplementedError

    def from_cpp(self, v):
        return v

    def literal(self, v) -> str:  # pragma: no cover - abstract
        raise TypeError(f"cannot write a C++ literal of type {self.key}")

    def fits(self, v) -> bool:
        return True

    def sample(self, rng, edge: bool):  # pragma: no cover - abstract
        raise TypeError(f"cannot generate values of type {self.key}")

    def __repr__(self):
        return f"<fleetpy type {self.key}>"

    def __eq__(self, other):
        return isinstance(other, FType) and self.key == other.key

    def __hash__(self):
        return hash(self.key)


# ---- scalars ---------------------------------------------------------------------------

class Bool(FType):
    cpp, key, category = "bool", "bool", "bool"

    def to_cpp(self, v, mod=None):
        if not isinstance(v, bool):
            raise TypeError(f"expected bool, got {type(v).__name__}: {v!r}")
        return v

    def literal(self, v):
        return "true" if self.to_cpp(v) else "false"

    def sample(self, rng, edge):
        return rng.choice([False, True])


class Int(FType):
    category = "int"

    def __init__(self, bits: int):
        self.bits, self.cpp, self.key = bits, f"int{bits}_t", f"i{bits}"
        self.lo, self.hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1

    def to_cpp(self, v, mod=None):
        if not is_int_value(v):   # refuse bool, so True never silently becomes 1
            raise TypeError(f"expected int for {self.key}, got {type(v).__name__}: {v!r}")
        if not self.lo <= v <= self.hi:
            raise OverflowError(f"{v} does not fit in {self.key} [{self.lo}, {self.hi}]")
        return v

    def literal(self, v):
        self.to_cpp(v)
        if v == self.lo:    # -2^(n-1) is not a C++ literal (it is -(2^(n-1)))
            return f"std::numeric_limits<{self.cpp}>::min()"
        return f"{self.cpp}({v}LL)"

    def fits(self, v):
        return is_int_value(v) and self.lo <= v <= self.hi

    def sample(self, rng, edge):
        if edge:
            return rng.choice([0, 1, -1, 2, -2, self.lo, self.hi, self.lo + 1, self.hi - 1, 7, -7])
        if rng.random() < 0.7:
            return rng.randint(max(self.lo, -20), min(self.hi, 20))
        return rng.randint(self.lo, self.hi)


class Float(FType):
    cpp, key, category = "double", "f64", "float"

    def to_cpp(self, v, mod=None):
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise TypeError(f"expected float, got {type(v).__name__}: {v!r}")
        return float(v)

    def literal(self, v):
        return cpp_double(self.to_cpp(v))

    def sample(self, rng, edge):
        if edge:
            return rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e-300, -1e-300, 1e300, -1e300,
                               math.inf, -math.inf, math.nan, 5e-324, 1.7976931348623157e308])
        return rng.choice([rng.gauss(0, 3), rng.uniform(-100, 100), float(rng.randint(-5, 5))])


class Str(FType):
    """std::string holding UTF-8. Transpiled code counts code points, as Python does."""
    cpp, key, category = "std::string", "str", "str"

    def headers(self):
        return ["<string>", "<nanobind/stl/string.h>"]

    def to_cpp(self, v, mod=None):
        if not isinstance(v, str):
            raise TypeError(f"expected str, got {type(v).__name__}: {v!r}")
        return v

    def literal(self, v):
        return cpp_string(self.to_cpp(v))

    def sample(self, rng, edge):
        pool = ["", "a", "b", "ab", "ba", "abc", "é", "日本", "á", "z" * 5]
        if edge or rng.random() < 0.5:
            return rng.choice(pool)
        return "".join(rng.choice("abcxyz") for _ in range(rng.randint(0, 6)))


class Char(FType):
    """C++ char; Python sees a one-character str. ASCII only, so that a Python character and
    a C++ byte are the same thing."""
    cpp, key, category = "char", "char", "char"

    def to_cpp(self, v, mod=None):
        if not self.fits(v):
            raise TypeError(f"expected a single ASCII character, got {v!r}")
        return v

    def literal(self, v):
        return f"char({ord(self.to_cpp(v))})"

    def fits(self, v):
        return isinstance(v, str) and len(v) == 1 and ord(v) < 128

    def sample(self, rng, edge):
        return rng.choice("ab01xyz ~\x00")


boolean, char, f64, string = Bool(), Char(), Float(), Str()
i8, i16, i32, i64 = Int(8), Int(16), Int(32), Int(64)

SCALARS = (Bool, Int, Float, Str)


# ---- containers ------------------------------------------------------------------------

class ListOf(FType):
    category = "list"

    def __init__(self, elem: FType):
        self.elem = elem
        self.cpp, self.key = f"std::vector<{elem.cpp}>", f"list[{elem.key}]"

    def children(self):
        return (self.elem,)

    def headers(self):
        return ["<vector>", "<nanobind/stl/vector.h>"]

    def to_cpp(self, v, mod=None):
        if not isinstance(v, (list, tuple)):
            raise TypeError(f"expected list for {self.key}, got {type(v).__name__}")
        return [self.elem.to_cpp(x, mod) for x in v]

    def from_cpp(self, v):
        return [self.elem.from_cpp(x) for x in v]

    def literal(self, v):
        return self.cpp + "{" + ", ".join(self.elem.literal(x) for x in v) + "}"

    def fits(self, v):
        return all(self.elem.fits(x) for x in v)

    def sample(self, rng, edge):
        return [self.elem.sample(rng, edge and rng.random() < 0.5) for _ in range(rng.randint(0, 4))]


class TupleOf(FType):
    category = "tuple"

    def __init__(self, elems):
        self.elems = tuple(elems)
        self.cpp = "std::tuple<" + ", ".join(e.cpp for e in self.elems) + ">"
        self.key = "tuple[" + ",".join(e.key for e in self.elems) + "]"

    def children(self):
        return self.elems

    def headers(self):
        return ["<tuple>", "<nanobind/stl/tuple.h>"]

    def to_cpp(self, v, mod=None):
        if not isinstance(v, tuple) or len(v) != len(self.elems):
            raise TypeError(f"expected a {len(self.elems)}-tuple for {self.key}, got {v!r}")
        return tuple(e.to_cpp(x, mod) for e, x in zip(self.elems, v))

    def from_cpp(self, v):
        return tuple(e.from_cpp(x) for e, x in zip(self.elems, v))

    def literal(self, v):
        return self.cpp + "{" + ", ".join(e.literal(x) for e, x in zip(self.elems, v)) + "}"

    def fits(self, v):
        return all(e.fits(x) for e, x in zip(self.elems, v))

    def sample(self, rng, edge):
        return tuple(e.sample(rng, edge) for e in self.elems)


class SetOf(FType):
    category = "set"

    def __init__(self, elem: FType):
        self.elem = elem
        self.cpp, self.key = f"std::set<{elem.cpp}>", f"frozenset[{elem.key}]"

    def children(self):
        return (self.elem,)

    def headers(self):
        return ["<set>", "<nanobind/stl/set.h>"]

    def to_cpp(self, v, mod=None):
        if not isinstance(v, (set, frozenset)):
            raise TypeError(f"expected frozenset for {self.key}, got {type(v).__name__}")
        return {self.elem.to_cpp(x, mod) for x in v}

    def from_cpp(self, v):
        return frozenset(self.elem.from_cpp(x) for x in v)

    def literal(self, v):
        return self.cpp + "{" + ", ".join(self.elem.literal(x) for x in sorted(v)) + "}"

    def sample(self, rng, edge):
        return frozenset(self.elem.sample(rng, edge) for _ in range(rng.randint(0, 4)))


# ---- user-defined types: these generate C++ definitions ----------------------------------

class StructType(FType):
    """A C++ struct generated from an ``@fl.struct`` dataclass."""
    category = "struct"

    def __init__(self, pycls, fields):
        self.pycls = pycls
        self.name = check_identifier(pycls.__name__, "struct name")
        self.fields = list(fields)   # [(name, FType)]
        self.cpp = self.name
        self.key = "struct " + self.name + "{" + ";".join(f"{n}:{t.key}" for n, t in self.fields) + "}"

    def field(self, name):
        return dict(self.fields).get(name)

    def children(self):
        return tuple(t for _, t in self.fields)

    def to_cpp(self, v, mod):
        if not isinstance(v, self.pycls):
            raise TypeError(f"expected {self.name}, got {type(v).__name__}: {v!r}")
        return getattr(mod, self.name)(*[t.to_cpp(getattr(v, n), mod) for n, t in self.fields])

    def from_cpp(self, v):
        return self.pycls(*[t.from_cpp(getattr(v, n)) for n, t in self.fields])

    def literal(self, v):
        if not isinstance(v, self.pycls):
            raise TypeError(f"expected {self.name}, got {v!r}")
        return f"{self.name}(" + ", ".join(t.literal(getattr(v, n)) for n, t in self.fields) + ")"

    def fits(self, v):
        return all(t.fits(getattr(v, n)) for n, t in self.fields)

    def sample(self, rng, edge):
        return self.pycls(*[t.sample(rng, edge and rng.random() < 0.5) for _, t in self.fields])


class NewType(FType):
    """A distinct C++ type wrapping a scalar, so that the grammar keeps it apart.

    Python sees the underlying value (an int, say); C++ sees a struct with a ``.v`` member
    that converts implicitly to and from the underlying type. Inside transpiled code it
    behaves as its base type (its ``category`` is the base's).
    """

    def __init__(self, name: str, base: FType):
        if not isinstance(base, SCALARS):
            raise TypeError("newtype only wraps scalar types (bool, ints, float, str)")
        self.name = check_identifier(name, "newtype name")
        self.base, self.category = base, base.category
        self.cpp, self.key = self.name, f"newtype {self.name}({base.key})"

    def headers(self):
        return self.base.headers()

    def to_cpp(self, v, mod):
        return getattr(mod, self.name)(self.base.to_cpp(v, mod))

    def from_cpp(self, v):
        return v.v

    def literal(self, v):
        return f"{self.name}({self.base.literal(v)})"

    def fits(self, v):
        return self.base.fits(v)

    def sample(self, rng, edge):
        return self.base.sample(rng, edge)


class OpaqueType(FType):
    """An existing C++ type from a header (e.g. std::bitset<16>, or your own class).

    With ``py=`` (int, float, str or bool) the value crosses the Python boundary as that
    type, through a nanobind type caster generated into each model module (so models never
    share or collide on it), converting with two C++ expressions in ``v``:
        from_py: Python-side value v (of py's C++ type) -> the opaque type
        to_py:   const opaque& v -> py's C++ type
    ``check`` validates Python values before conversion. Without ``py=`` the type can be
    used inside the grammar but cannot cross the boundary.
    """
    category = "opaque"

    def __init__(self, cpp_name: str, header: str, py=None, from_py=None, to_py=None, check=None):
        self.cpp, self.header = cpp_name, header
        self.py = resolve(py) if py is not None else None
        if self.py is not None:
            if not isinstance(self.py, SCALARS):
                raise TypeError("opaque py= must be int, float, str or bool (or a fleetpy int width)")
            if not (isinstance(from_py, str) and isinstance(to_py, str)):
                raise TypeError("opaque types with py= need from_py= and to_py= C++ expressions in v")
        elif from_py or to_py:
            raise TypeError("from_py/to_py need py=")
        self.from_py, self.to_py, self.check = from_py, to_py, check
        self.key = (f"opaque {cpp_name} @ {header} py={self.py.key if self.py else None} "
                    f"from={from_py} to={to_py}")

    def headers(self):
        return [self.header if self.header.startswith("<") else f'"{self.header}"']

    def _need_py(self):
        if self.py is None:
            raise TypeError(f"opaque type {self.cpp} has no Python conversion (give py=, from_py=, to_py=)")

    def to_cpp(self, v, mod=None):
        self._need_py()
        v = self.py.to_cpp(v, mod)
        if self.check is not None and not self.check(v):
            raise ValueError(f"{v!r} is not a valid {self.cpp}")
        return v   # the generated type caster converts it to the C++ type

    def from_cpp(self, v):
        self._need_py()
        return v

    def literal(self, v):
        self.to_cpp(v)
        return (f"([]() {{ const {self.py.cpp} v = {self.py.literal(v)}; "
                f"return {self.cpp}({self.from_py}); }}())")

    def sample(self, rng, edge):
        self._need_py()
        for _ in range(100):
            v = self.py.sample(rng, edge)
            if self.check is None or self.check(v):
                return v
        raise TypeError(f"cannot generate values for {self.cpp}: its check rejects 100 random {self.py.key}s")


# ---- traversal -----------------------------------------------------------------------------

def walk(*types):
    """Every type reachable from ``types`` (each once, children before parents)."""
    seen, out = set(), []

    def visit(t):
        if t.key in seen:
            return
        for c in t.children():
            visit(c)
        seen.add(t.key)
        out.append(t)
    for t in types:
        visit(t)
    return out


def definitions(*types):
    """The struct and newtype definitions ``types`` need, dependencies first."""
    return [t for t in walk(*types) if isinstance(t, (StructType, NewType))]


def headers(*types):
    return list(dict.fromkeys(h for t in walk(*types) for h in t.headers()))


# ---- constructors ----------------------------------------------------------------------------

def newtype(name: str, base) -> NewType:
    return NewType(name, resolve(base))


def opaque(cpp_name: str, header: str, *, py=None, from_py=None, to_py=None, check=None) -> OpaqueType:
    return OpaqueType(cpp_name, header, py, from_py, to_py, check)


def struct(cls):
    """Decorator: turn an annotated class into a frozen dataclass with a C++ twin."""
    if not getattr(cls, "__annotations__", None):
        raise TypeError(f"@fl.struct class {cls.__name__} has no annotated fields")
    dc = dataclasses.dataclass(frozen=True, order=True)(cls)
    hints = typing.get_type_hints(cls, include_extras=True)
    fields = [(check_identifier(f.name, f"field of {cls.__name__}"), resolve(hints[f.name]))
              for f in dataclasses.fields(dc)]
    dc.__fleet_type__ = StructType(dc, fields)
    return dc


_BUILTIN_ANNOTATIONS = {bool: boolean, int: i64, float: f64, str: string}


def resolve(t) -> FType:
    """Map a Python annotation to its FType."""
    if isinstance(t, FType):
        return t
    if t in _BUILTIN_ANNOTATIONS:
        return _BUILTIN_ANNOTATIONS[t]
    if isinstance(t, type) and hasattr(t, "__fleet_type__"):
        return t.__fleet_type__
    origin, args = typing.get_origin(t), typing.get_args(t)
    if origin is list:
        return ListOf(resolve(*args))
    if origin is tuple:
        if len(args) == 2 and args[1] is Ellipsis:
            raise TypeError("variable-length tuples are not supported; use list[T]")
        return TupleOf([resolve(a) for a in args])
    if origin is frozenset:
        return SetOf(resolve(*args))
    if origin is set:
        raise TypeError("use frozenset[T] (immutable) rather than set[T]")
    raise TypeError(f"no fleetpy type for annotation {t!r}")


def infer(value) -> FType:
    """The type of a Python constant: bool, int (i64), float or str."""
    for py, t in ((bool, boolean), (int, i64), (float, f64), (str, string)):
        if isinstance(value, py):
            return t
    raise TypeError(f"cannot infer a fleetpy type for {value!r}; give the type explicitly")
