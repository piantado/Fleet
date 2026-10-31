"""
The fleetpy type system.

Every Python annotation used in a model maps to exactly one C++ type, fixed at build
time. A ``FType`` knows:

  * ``cpp``      -- the C++ spelling of the type
  * ``to_cpp``   -- convert/validate a Python value into what the compiled module accepts
  * ``from_cpp`` -- convert a value returned by the compiled module back to Python
  * ``key``      -- a canonical string used for hashing and for deduplicating grammar types

Fleet keys grammar nonterminals by C++ type, so two annotations that map to the same
C++ type are the *same* nonterminal. Use ``newtype`` to make distinct roles distinct.
"""

from __future__ import annotations

import dataclasses
import keyword
import re
import typing

# C++ keywords (and a few reserved names) that cannot be used as identifiers in generated code.
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
    """Raise if ``name`` cannot be used verbatim as a C++ (and Python) identifier."""
    if not isinstance(name, str) or not _IDENT.match(name):
        raise ValueError(f"{what} {name!r} is not a valid identifier")
    if name in CPP_KEYWORDS or keyword.iskeyword(name):
        raise ValueError(f"{what} {name!r} is a C++ or Python keyword; rename it")
    if name.startswith("fp_") or name.startswith("__"):
        raise ValueError(f"{what} {name!r} uses a prefix reserved for generated code (fp_ or __)")
    return name


class FType:
    """Base class of all fleetpy types."""

    cpp: str
    key: str

    def to_cpp(self, v, mod):  # pragma: no cover - abstract
        raise NotImplementedError

    def from_cpp(self, v):
        return v

    # struct definitions this type depends on (in dependency order)
    def structs(self) -> list:
        return []

    def headers(self) -> list:
        return []

    def __repr__(self):
        return f"<fleetpy type {self.key}>"

    def __eq__(self, other):
        return isinstance(other, FType) and self.key == other.key

    def __hash__(self):
        return hash(self.key)


class _Bool(FType):
    cpp = "bool"
    key = "bool"

    def to_cpp(self, v, mod):
        if not isinstance(v, bool):
            raise TypeError(f"expected bool, got {type(v).__name__}: {v!r}")
        return v


class _Int(FType):
    def __init__(self, bits: int):
        self.bits = bits
        self.cpp = f"int{bits}_t"
        self.key = f"i{bits}"
        self.lo = -(1 << (bits - 1))
        self.hi = (1 << (bits - 1)) - 1

    def to_cpp(self, v, mod):
        # bool is a subclass of int in Python; refuse it so True never silently becomes 1
        if isinstance(v, bool) or not isinstance(v, int):
            raise TypeError(f"expected int for {self.key}, got {type(v).__name__}: {v!r}")
        if not (self.lo <= v <= self.hi):
            raise OverflowError(f"{v} does not fit in {self.key} [{self.lo}, {self.hi}]")
        return v


class _Float(FType):
    cpp = "double"
    key = "f64"

    def to_cpp(self, v, mod):
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise TypeError(f"expected float, got {type(v).__name__}: {v!r}")
        return float(v)


class _Str(FType):
    cpp = "std::string"
    key = "str"

    def headers(self):
        return ["<string>", "nanobind/stl/string.h"]

    def to_cpp(self, v, mod):
        if not isinstance(v, str):
            raise TypeError(f"expected str, got {type(v).__name__}: {v!r}")
        return v


class _Char(FType):
    """C++ char. Python sees a one-character str; restricted to ASCII so that a Python
    character and a C++ byte are the same thing."""
    cpp = "char"
    key = "char"

    def to_cpp(self, v, mod):
        if not (isinstance(v, str) and len(v) == 1 and ord(v) < 128):
            raise TypeError(f"expected a single ASCII character, got {v!r}")
        return v


boolean = _Bool()
char = _Char()
i8, i16, i32, i64 = _Int(8), _Int(16), _Int(32), _Int(64)
f64 = _Float()
string = _Str()


class ListOf(FType):
    def __init__(self, elem: FType):
        self.elem = elem
        self.cpp = f"std::vector<{elem.cpp}>"
        self.key = f"list[{elem.key}]"

    def structs(self):
        return self.elem.structs()

    def headers(self):
        return ["<vector>", "nanobind/stl/vector.h"] + self.elem.headers()

    def to_cpp(self, v, mod):
        if not isinstance(v, (list, tuple)):
            raise TypeError(f"expected list for {self.key}, got {type(v).__name__}")
        return [self.elem.to_cpp(x, mod) for x in v]

    def from_cpp(self, v):
        return [self.elem.from_cpp(x) for x in v]


class TupleOf(FType):
    def __init__(self, elems):
        self.elems = tuple(elems)
        self.cpp = "std::tuple<" + ", ".join(e.cpp for e in self.elems) + ">"
        self.key = "tuple[" + ",".join(e.key for e in self.elems) + "]"

    def structs(self):
        out = []
        for e in self.elems:
            out += e.structs()
        return out

    def headers(self):
        h = ["<tuple>", "nanobind/stl/tuple.h"]
        for e in self.elems:
            h += e.headers()
        return h

    def to_cpp(self, v, mod):
        if not isinstance(v, tuple) or len(v) != len(self.elems):
            raise TypeError(f"expected a {len(self.elems)}-tuple for {self.key}, got {v!r}")
        return tuple(e.to_cpp(x, mod) for e, x in zip(self.elems, v))

    def from_cpp(self, v):
        return tuple(e.from_cpp(x) for e, x in zip(self.elems, v))


class SetOf(FType):
    def __init__(self, elem: FType):
        self.elem = elem
        self.cpp = f"std::set<{elem.cpp}>"
        self.key = f"frozenset[{elem.key}]"

    def structs(self):
        return self.elem.structs()

    def headers(self):
        return ["<set>", "nanobind/stl/set.h"] + self.elem.headers()

    def to_cpp(self, v, mod):
        if not isinstance(v, (set, frozenset)):
            raise TypeError(f"expected frozenset for {self.key}, got {type(v).__name__}")
        return {self.elem.to_cpp(x, mod) for x in v}

    def from_cpp(self, v):
        return frozenset(self.elem.from_cpp(x) for x in v)


class StructType(FType):
    """A C++ struct generated from an ``@fl.struct`` dataclass."""

    def __init__(self, pycls, fields):
        self.pycls = pycls
        self.name = check_identifier(pycls.__name__, "struct name")
        self.fields = fields  # list of (name, FType)
        self.cpp = self.name
        self.key = "struct " + self.name + "{" + ";".join(f"{n}:{t.key}" for n, t in fields) + "}"

    def structs(self):
        out = []
        for _, t in self.fields:
            out += t.structs()
        out.append(self)
        return out

    def headers(self):
        h = []
        for _, t in self.fields:
            h += t.headers()
        return h

    def to_cpp(self, v, mod):
        if not isinstance(v, self.pycls):
            raise TypeError(f"expected {self.name}, got {type(v).__name__}: {v!r}")
        cls = getattr(mod, self.name)
        return cls(*[t.to_cpp(getattr(v, n), mod) for n, t in self.fields])

    def from_cpp(self, v):
        return self.pycls(*[t.from_cpp(getattr(v, n)) for n, t in self.fields])


class NewType(FType):
    """A distinct C++ type wrapping a scalar, so that the grammar keeps it apart.

    Python sees the underlying value (e.g. an int); C++ sees a distinct struct with a
    ``.v`` member that converts implicitly to and from the underlying type.
    """

    def __init__(self, name: str, base: FType):
        if not isinstance(base, (_Int, _Float, _Bool, _Str)):
            raise TypeError("newtype only wraps scalar types (bool, ints, float, str)")
        self.name = check_identifier(name, "newtype name")
        self.base = base
        self.cpp = self.name
        self.key = f"newtype {self.name}({base.key})"

    def structs(self):
        return [self]

    def headers(self):
        return self.base.headers()

    def to_cpp(self, v, mod):
        return getattr(mod, self.name)(self.base.to_cpp(v, mod))

    def from_cpp(self, v):
        return v.v


class OpaqueType(FType):
    """An existing C++ type from a header (e.g. std::bitset<16>, or your own class).

    With ``py=`` (a scalar Python type: int, float, str or bool) the value crosses the
    Python boundary as that type. fleetpy generates a nanobind type caster (compiled into
    each model module, so models never share or collide on it) that converts with two C++
    expressions in ``v``:
        from_py: Python-side value v (of py's C++ type) -> the opaque type
        to_py:   const opaque& v -> py's C++ type
    ``check`` validates Python values before they are converted. Without ``py=`` the type
    can be used inside the grammar but cannot cross the boundary.
    """

    def __init__(self, cpp_name: str, header: str, py=None, from_py=None, to_py=None, check=None):
        self.cpp = cpp_name
        self.header = header
        self.py = resolve(py) if py is not None else None
        if self.py is not None:
            if not isinstance(self.py, (_Int, _Float, _Str, _Bool)):
                raise TypeError("opaque py= must be int, float, str or bool (or a fleetpy int width)")
            if not (isinstance(from_py, str) and isinstance(to_py, str)):
                raise TypeError("opaque types with py= need from_py= and to_py= C++ expressions in v")
        elif from_py or to_py:
            raise TypeError("from_py/to_py need py=")
        self.from_py, self.to_py, self.check = from_py, to_py, check
        self.key = f"opaque {cpp_name} @ {header} py={self.py.key if self.py else None} " \
                   f"from={from_py} to={to_py}"

    def headers(self):
        return [self.header if self.header.startswith("<") else f'"{self.header}"']

    def to_cpp(self, v, mod):
        if self.py is None:
            raise TypeError(f"opaque type {self.cpp} has no Python conversion (give py=, from_py=, to_py=)")
        v = self.py.to_cpp(v, mod)
        if self.check is not None and not self.check(v):
            raise ValueError(f"{v!r} is not a valid {self.cpp}")
        return v   # the generated type caster converts it to the C++ type

    def from_cpp(self, v):
        if self.py is None:
            raise TypeError(f"opaque type {self.cpp} has no Python conversion (give py=, from_py=, to_py=)")
        return v


def newtype(name: str, base) -> NewType:
    return NewType(name, resolve(base))


def opaque(cpp_name: str, header: str, *, py=None, from_py=None, to_py=None, check=None) -> OpaqueType:
    return OpaqueType(cpp_name, header, py, from_py, to_py, check)


def struct(cls):
    """Decorator: turn an annotated class into a frozen dataclass with a C++ twin."""
    if not hasattr(cls, "__annotations__") or not cls.__annotations__:
        raise TypeError(f"@fl.struct class {cls.__name__} has no annotated fields")
    dc = dataclasses.dataclass(frozen=True, order=True)(cls)
    hints = typing.get_type_hints(cls, include_extras=True)
    fields = []
    for f in dataclasses.fields(dc):
        check_identifier(f.name, f"field of {cls.__name__}")
        fields.append((f.name, resolve(hints[f.name])))
    dc.__fleet_type__ = StructType(dc, fields)
    return dc


def resolve(t) -> FType:
    """Map a Python annotation to its FType."""
    if isinstance(t, FType):
        return t
    if t is bool:
        return boolean
    if t is int:
        return i64
    if t is float:
        return f64
    if t is str:
        return string
    if isinstance(t, type) and hasattr(t, "__fleet_type__"):
        return t.__fleet_type__
    origin = typing.get_origin(t)
    args = typing.get_args(t)
    if origin is list:
        (a,) = args
        return ListOf(resolve(a))
    if origin is tuple:
        if len(args) == 2 and args[1] is Ellipsis:
            raise TypeError("variable-length tuples are not supported; use list[T]")
        return TupleOf([resolve(a) for a in args])
    if origin is frozenset:
        (a,) = args
        return SetOf(resolve(a))
    if origin is set:
        raise TypeError("use frozenset[T] (immutable) rather than set[T]")
    raise TypeError(f"no fleetpy type for annotation {t!r}")
