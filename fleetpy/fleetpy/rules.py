"""
Grammar rules. Every rule, whatever its kind, has the same face:

  fmt   the format Fleet prints it with ("f(%s,%s)", "x", ...)
  p     its (unnormalized) prior weight among the rules of its type
  ret   the FType it produces (its nonterminal)
  args  the FTypes of its children
  cpp_registration(G)  the Grammar::add / add_terminal call that registers it in C++

Three kinds of rule exist: a ``Primitive`` (a user function: C++, transpiled Python, or a
Python callback), a ``Terminal`` (a constant), and a ``Builtin`` (one of Fleet's VM-level
builtins, never re-implemented here). Fleet's builtins are described by one table,
``BUILTINS``; ``fl.builtins`` exposes it.
"""

from __future__ import annotations

import dataclasses
import inspect
import re
import typing

from . import ftypes as T


def default_format(name: str, arity: int) -> str:
    """Fleet's conventional format: ``f(%s,%s)``, or just ``f`` for no arguments.

    This is the shape ``Grammar::simple_parse`` reconstructs from a printed program, so
    default formats always round-trip through ``Model.parse``.
    """
    return name if arity == 0 else name + "(" + ",".join(["%s"] * arity) + ")"


# A canonical format is a head token (no parentheses, commas, whitespace or %) optionally
# followed by "(%s,...)" or "()": "f(%s,%s)", "x", "flip()", "'0'", "Ø". Printed programs over
# canonical formats are unambiguous, and fleetpy's parser reads them.
_CANONICAL = re.compile(r"^[^(),\s%]+(\(((%s)(,%s)*)?\))?$")


def is_canonical_format(fmt: str) -> bool:
    return bool(_CANONICAL.match(fmt))


def _check_p(p, what):
    if not (isinstance(p, (int, float)) and not isinstance(p, bool) and p > 0):
        raise ValueError(f"{what}: p must be a positive number, got {p!r}")
    return float(p)


def _check_fmt(fmt, arity, what):
    if not isinstance(fmt, str) or not fmt:
        raise ValueError(f"{what}: the format must be a non-empty string, got {fmt!r}")
    if fmt.count("%s") != arity:
        raise ValueError(f"{what}: format {fmt!r} has {fmt.count('%s')} %s placeholders "
                         f"but takes {arity} arguments")
    return fmt


class Rule:
    fmt: str
    p: float
    ret: T.FType
    args: tuple

    @property
    def arity(self) -> int:
        return len(self.args)

    def cpp_registration(self, G: str) -> str:  # pragma: no cover - abstract
        raise NotImplementedError

    def spec(self) -> dict:  # pragma: no cover - abstract
        """A canonical description for the build hash."""
        raise NotImplementedError


class Primitive(Rule):
    """A rule backed by a user function.

    tier 1: a C++ body (``cpp=``); tier 2: the Python body, transpiled to C++;
    tier 3: a call back into Python (``backend="python"``).
    """

    def __init__(self, fn, fmt=None, *, p=1.0, cpp=None, backend="cpp", max_iters=1_000_000):
        self.fn = fn
        self.name = T.check_identifier(fn.__name__, "primitive name")
        what = f"primitive {self.name}"
        hints = typing.get_type_hints(fn)
        params = []
        for pname, par in inspect.signature(fn).parameters.items():
            if par.kind not in (par.POSITIONAL_ONLY, par.POSITIONAL_OR_KEYWORD):
                raise TypeError(f"{what}: only plain positional parameters are allowed")
            if par.default is not par.empty:
                raise TypeError(f"{what}: parameters cannot have defaults")
            if pname not in hints:
                raise TypeError(f"{what}: parameter {pname!r} needs a type annotation")
            params.append((T.check_identifier(pname, f"parameter of {self.name}"), T.resolve(hints[pname])))
        if "return" not in hints:
            raise TypeError(f"{what}: needs a return type annotation")
        if len(params) > 8:   # Grammar::add dispatches on at most 8 arguments
            raise TypeError(f"{what}: Fleet supports at most 8 arguments, got {len(params)}")
        self.params = params
        self.args = tuple(t for _, t in params)
        self.ret = T.resolve(hints["return"])
        self.fmt = _check_fmt(default_format(self.name, self.arity) if fmt is None else fmt, self.arity, what)
        self.p = _check_p(p, what)
        if not (isinstance(max_iters, int) and max_iters >= 1):
            raise ValueError("max_iters must be an int >= 1")
        self.max_iters = max_iters
        if backend not in ("cpp", "python"):
            raise ValueError("backend must be 'cpp' or 'python'")
        if backend == "python" and cpp is not None:
            raise ValueError(f"{what}: cpp= and backend='python' are exclusive")
        self.cpp = cpp
        self.tier = 3 if backend == "python" else (1 if cpp is not None else 2)

    def cpp_registration(self, G):
        return f"add({T.cpp_string_literal(self.fmt)}, &fp_user::{self.name}, {self.p!r})"

    def spec(self):
        return {"kind": "primitive", "name": self.name, "fmt": self.fmt, "p": self.p, "tier": self.tier,
                "params": [[n, t.key] for n, t in self.params], "ret": self.ret.key, "cpp": self.cpp,
                "max_iters": self.max_iters,
                "src": _source(self.fn) if self.tier == 2 else None,
                "pyid": f"{self.fn.__module__}.{self.fn.__qualname__}" if self.tier == 3 else None}

    def __repr__(self):
        return f"<primitive {self.fmt} tier={self.tier} p={self.p}>"


class Terminal(Rule):
    """A constant (``Grammar::add_terminal``)."""

    args = ()

    def __init__(self, fmt, value, type_, p=1.0):
        if not isinstance(fmt, str) or not fmt or "%s" in fmt:
            raise ValueError(f"terminal format must be a non-empty string without %s, got {fmt!r}")
        self.fmt, self.value, self.ret = fmt, value, T.resolve(type_)
        self.p = _check_p(p, f"terminal {fmt!r}")
        self.cpp_value = self.ret.literal(value)   # validates the value against the type

    def cpp_registration(self, G):
        return f"add_terminal({T.cpp_string_literal(self.fmt)}, {self.cpp_value}, {self.p!r})"

    def spec(self):
        return {"kind": "terminal", "fmt": self.fmt, "p": self.p, "type": self.ret.key, "value": self.cpp_value}

    def __repr__(self):
        return f"<terminal {self.fmt}={self.value!r}:{self.ret.key} p={self.p}>"


# ---- Fleet's builtins ------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class BuiltinSpec:
    """One of Fleet's builtins (src/VirtualMachine/Builtins.h).

    The signature is symbolic: "in"/"out" are the grammar's input and output types, "T" is
    the builtin's type parameter (``B.If(int)``), "set[T]" a frozenset of it, and "K" the
    lexicon key type (``B.LexiconRecurse(key=int)``).
    """
    name: str
    fmt: str
    ret: str
    args: tuple
    param: str = None         # None | "type" | "key"
    stochastic: bool = False  # forks the VM: needs a stochastic hypothesis
    recursion: str = None     # None | "self" | "lexicon"
    memo: bool = False
    safe: bool = False        # returns output_t{} when the argument's size() is 0


_LOGIC2 = ("bool", "bool")
_REC = ("in",)
_LEXREC = ("K", "in")   # Builtins::LexiconRecurse is Primitive<output_t, key_t, input_t>

BUILTINS = [BuiltinSpec(*a, **kw) for *a, kw in [
    ("X", "x", "in", (), {}),
    ("And", "and(%s,%s)", "bool", _LOGIC2, {}),
    ("Or", "or(%s,%s)", "bool", _LOGIC2, {}),
    ("Not", "not(%s)", "bool", ("bool",), {}),
    ("Implies", "implies(%s,%s)", "bool", _LOGIC2, {}),
    ("Iff", "iff(%s,%s)", "bool", _LOGIC2, {}),
    ("If", "if(%s,%s,%s)", "T", ("bool", "T", "T"), {"param": "type"}),
    ("Flip", "flip()", "bool", (), {"stochastic": True}),
    ("FlipP", "flipp(%s)", "bool", ("f64",), {"stochastic": True}),
    ("SafeFlipP", "safe_flipp(%s)", "bool", ("f64",), {"stochastic": True}),
    ("Sample", "sample(%s)", "T", ("set[T]",), {"param": "type", "stochastic": True}),
    ("Recurse", "recurse(%s)", "out", _REC, {"recursion": "self"}),
    ("SafeRecurse", "safe_recurse(%s)", "out", _REC, {"recursion": "self", "safe": True}),
    ("MemRecurse", "mem_recurse(%s)", "out", _REC, {"recursion": "self", "memo": True}),
    ("SafeMemRecurse", "safe_mem_recurse(%s)", "out", _REC, {"recursion": "self", "memo": True, "safe": True}),
    ("LexiconRecurse", "F(%s,%s)", "out", _LEXREC, {"param": "key", "recursion": "lexicon"}),
    ("LexiconSafeRecurse", "safe_F(%s,%s)", "out", _LEXREC, {"param": "key", "recursion": "lexicon", "safe": True}),
    ("LexiconMemRecurse", "mem_F(%s,%s)", "out", _LEXREC, {"param": "key", "recursion": "lexicon", "memo": True}),
    ("LexiconSafeMemRecurse", "safe_mem_F(%s,%s)", "out", _LEXREC,
     {"param": "key", "recursion": "lexicon", "memo": True, "safe": True}),
]]


def lexicon_key_type(t) -> T.FType:
    """Fleet's ProgramLoader dispatches lexicon keys only on short, int and std::string."""
    t = T.resolve(t)
    if t == T.i64:        # Python int keys map to C++ int
        t = T.i32
    if t not in (T.i16, T.i32, T.string):
        raise TypeError("lexicon keys must be fl.i16, int/fl.i32 or str (Fleet's ProgramLoader "
                        "dispatches only on short, int and std::string)")
    return t


class Builtin(Rule):
    """A use of a Fleet builtin. Its signature is resolved when it is added to a grammar."""

    def __init__(self, spec: BuiltinSpec, fmt=None, p=1.0, param: T.FType = None, grammar=None):
        self.builtin, self.param = spec, param
        what = f"builtin {spec.name}"
        self.fmt = _check_fmt(spec.fmt if fmt is None else fmt, len(spec.args), what)
        self.p = _check_p(p, what)
        self.ret = self.args = None
        if grammar is not None:
            sym = {"in": grammar.input, "out": grammar.output, "bool": T.boolean, "f64": T.f64,
                   "T": param, "set[T]": T.SetOf(param) if param is not None else None, "K": param}
            self.ret, self.args = sym[spec.ret], tuple(sym[a] for a in spec.args)

    def bind(self, grammar) -> "Builtin":
        return Builtin(self.builtin, self.fmt, self.p, self.param, grammar)

    def cpp_template(self, G):
        ps = [G] + ([self.param.cpp] if self.param is not None else [])
        return f"Builtins::{self.builtin.name}<{','.join(ps)}>"

    def cpp_registration(self, G):
        return f"add({T.cpp_string_literal(self.fmt)}, {self.cpp_template(G)}, {self.p!r})"

    def spec(self):
        return {"kind": "builtin", "builtin": self.builtin.name, "fmt": self.fmt, "p": self.p,
                "param": self.param.key if self.param is not None else None}

    def __repr__(self):
        return f"<builtin {self.builtin.name} {self.fmt} p={self.p}>"


class BuiltinFactory:
    """``B.And`` (as is) or ``B.And(p=2.0, fmt="&(%s,%s)")``; type-parameterized builtins
    take their type positionally (``B.If(int)``), lexicon recursions as ``key=``."""

    def __init__(self, spec: BuiltinSpec):
        self.spec = spec

    def __call__(self, *targs, p=1.0, fmt=None, key=None) -> Builtin:
        s, param = self.spec, None
        if s.param == "type":
            if len(targs) != 1:
                raise TypeError(f"{s.name} needs exactly one type argument, e.g. B.{s.name}(int)")
            param = T.resolve(targs[0])
        elif targs:
            raise TypeError(f"{s.name} takes no positional type argument")
        if s.param == "key":
            if key is None:
                raise TypeError(f"{s.name} needs the lexicon's key type: B.{s.name}(key=int) or key=str")
            param = lexicon_key_type(key)
        elif key is not None:
            raise TypeError(f"{s.name} takes no key")
        return Builtin(s, fmt, p, param)

    def default(self) -> Builtin:
        if self.spec.param is not None:
            example = "key=int" if self.spec.param == "key" else "int"
            raise TypeError(f"B.{self.spec.name} needs its {self.spec.param}: B.{self.spec.name}({example})")
        return self()

    def __repr__(self):
        return f"<fleetpy builtin {self.spec.name}>"


class builtins:
    """Fleet's builtins, with Fleet's usual formats ("x", "and(%s,%s)", "flip()", ...)."""


for _s in BUILTINS:
    setattr(builtins, _s.name, BuiltinFactory(_s))


def _source(fn):
    try:
        return inspect.getsource(fn)
    except (OSError, TypeError):
        return None
