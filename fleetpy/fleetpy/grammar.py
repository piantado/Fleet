"""
Grammar specification: primitives, builtins, and the checks run on them.

Nothing here compiles anything. Decorators only record a spec; ``fleetpy.build`` turns
the spec into C++.
"""

from __future__ import annotations

import inspect
import re
import typing

from . import ftypes as T


def default_format(name: str, arity: int) -> str:
    """Fleet's conventional format: ``f(%s,%s)``, or just ``f`` for a terminal.

    This is exactly the shape that ``Grammar::simple_parse`` reconstructs from a printed
    program, so default formats always round-trip through ``Model.parse``.
    """
    if arity == 0:
        return name
    return name + "(" + ",".join(["%s"] * arity) + ")"


# A "canonical" format is a head token (no parentheses, commas or whitespace) optionally
# followed by "(%s,...)" or "()". Printed programs over canonical formats are unambiguous
# and fleetpy's parser handles them; e.g. "f(%s,%s)", "x", "flip()", "'0'", "\u00D8".
_CANONICAL = re.compile(r"^[^(),\s%]+(\(((%s)(,%s)*)?\))?$")


def is_canonical_format(fmt: str) -> bool:
    """True if ``fmt`` has the ``head`` / ``head(%s,...)`` / ``head()`` shape fleetpy can parse."""
    return bool(_CANONICAL.match(fmt))


class Primitive:
    """One grammar rule backed by a user function (tier 1, 2 or 3)."""

    def __init__(self, fn, fmt, p, cpp, backend, max_iters=1_000_000):
        self.fn = fn
        if not (isinstance(max_iters, int) and max_iters >= 1):
            raise ValueError("max_iters must be an int >= 1")
        self.max_iters = max_iters
        self.name = T.check_identifier(fn.__name__, "primitive name")
        sig = inspect.signature(fn)
        hints = typing.get_type_hints(fn)
        self.argnames = []
        self.argtypes = []
        for pname, par in sig.parameters.items():
            if par.kind not in (par.POSITIONAL_ONLY, par.POSITIONAL_OR_KEYWORD):
                raise TypeError(f"primitive {self.name}: only plain positional parameters are allowed")
            if par.default is not par.empty:
                raise TypeError(f"primitive {self.name}: parameters cannot have defaults")
            if pname not in hints:
                raise TypeError(f"primitive {self.name}: parameter {pname!r} needs a type annotation")
            T.check_identifier(pname, f"parameter of {self.name}")
            self.argnames.append(pname)
            self.argtypes.append(T.resolve(hints[pname]))
        if "return" not in hints:
            raise TypeError(f"primitive {self.name}: needs a return type annotation")
        self.rettype = T.resolve(hints["return"])
        self.arity = len(self.argtypes)
        if self.arity > 8:
            # Grammar::add's dispatch switch in Fleet handles at most 8 arguments
            raise TypeError(f"primitive {self.name}: Fleet supports at most 8 arguments, got {self.arity}")
        self.fmt = fmt if fmt is not None else default_format(self.name, self.arity)
        if self.fmt.count("%s") != self.arity:
            raise ValueError(
                f"primitive {self.name}: format {self.fmt!r} has {self.fmt.count('%s')} %s "
                f"placeholders but the function takes {self.arity} arguments")
        if not (isinstance(p, (int, float)) and p > 0):
            raise ValueError(f"primitive {self.name}: p must be a positive number, got {p!r}")
        self.p = float(p)
        if backend not in ("cpp", "python"):
            raise ValueError("backend must be 'cpp' or 'python'")
        if backend == "python" and cpp is not None:
            raise ValueError(f"primitive {self.name}: cpp= and backend='python' are exclusive")
        self.backend = backend
        self.cpp = cpp
        # tier: 1 = C++ snippet, 2 = transpiled Python, 3 = Python callback
        self.tier = 3 if backend == "python" else (1 if cpp is not None else 2)
        self.transpiled = None  # filled in at build time for tier 2

    def __repr__(self):
        return f"<primitive {self.fmt} tier={self.tier} p={self.p}>"


class Builtin:
    """One of Fleet's VM-level builtins (Builtins::And etc.)."""

    def __init__(self, kind, fmt, p, sig, cpp_template, *, stochastic=False, recursion=None,
                 memo=False, safe=False, key=None):
        self.kind = kind
        self.fmt = fmt
        if not (isinstance(p, (int, float)) and p > 0):
            raise ValueError(f"builtin {kind}: p must be a positive number, got {p!r}")
        self.p = float(p)
        self.sig = sig                   # function (grammar) -> (rettype, [argtypes])
        self.cpp_template = cpp_template  # {G} = grammar class name
        self.stochastic = stochastic      # needs a VirtualMachinePool (StochasticHypothesis)
        self.recursion = recursion        # None | "plain" | "lexicon"
        self.memo = memo                  # memoized recursion
        self.safe = safe                  # Safe*: returns output_t{} when the argument's size() is 0
        self.key = key                    # FType of the lexicon key (lexicon recursion only)

    def __repr__(self):
        return f"<builtin {self.kind} {self.fmt} p={self.p}>"


class Terminal:
    """A constant: ``Grammar::add_terminal(fmt, value, p)``."""

    def __init__(self, fmt, value, type_, p):
        if not isinstance(fmt, str) or not fmt or "%s" in fmt:
            raise ValueError(f"terminal format must be a non-empty string without %s, got {fmt!r}")
        if not (isinstance(p, (int, float)) and p > 0):
            raise ValueError(f"terminal {fmt!r}: p must be a positive number, got {p!r}")
        self.fmt, self.value, self.type, self.p = fmt, value, type_, float(p)
        if not isinstance(self.type, (T.StructType, T.NewType, T.OpaqueType)):
            self.type.to_cpp(value, None)  # validates the value against the type now

    def __repr__(self):
        return f"<terminal {self.fmt}={self.value!r}:{self.type.key} p={self.p}>"


class _BuiltinFactory:
    """Callable/attribute object producing Builtin specs.

    ``B.And`` and ``B.And(p=2.0, fmt="&(%s,%s)")`` both work. Builtins parameterized by a
    type take it positionally (``B.If(int)``, ``B.Sample(str)``) or as ``key=`` for the
    lexicon recursions (``B.LexiconRecurse(key=int)``).
    """

    def __init__(self, kind, fmt, sig, cpp_template, needs_type=False, needs_key=False, **flags):
        self.kind, self._fmt, self.sig, self.cpp_template = kind, fmt, sig, cpp_template
        self.needs_type, self.needs_key, self.flags = needs_type, needs_key, flags

    def __call__(self, *targs, p=1.0, fmt=None, key=None):
        sig, tmpl, keyt = self.sig, self.cpp_template, None
        if self.needs_type:
            if len(targs) != 1:
                raise TypeError(f"{self.kind} needs exactly one type argument, e.g. B.{self.kind}(int)")
            t = T.resolve(targs[0])
            sig = (lambda g, t=t, f=self.sig: f(g, t))
            tmpl = tmpl.replace("{T}", t.cpp)
        elif targs:
            raise TypeError(f"{self.kind} takes no positional type argument")
        if self.needs_key:
            if key is None:
                raise TypeError(f"{self.kind} needs the lexicon's key type: B.{self.kind}(key=int) or key=str")
            keyt = T.resolve(key)
            if keyt.key == "i64":     # Python int keys map to C++ int (see Lexicon)
                keyt = T.i32
            if keyt.key not in ("i16", "i32", "str"):
                raise TypeError("lexicon keys must be fl.i16, int/fl.i32 or str (Fleet's ProgramLoader "
                                "dispatches only on short, int and std::string)")
            sig = (lambda g, k=keyt, f=self.sig: f(g, k))
            tmpl = tmpl.replace("{K}", keyt.cpp)
        elif key is not None:
            raise TypeError(f"{self.kind} takes no key")
        f = fmt if fmt is not None else self._fmt
        n = len(sig(_SigProbe())[1])
        if f.count("%s") != n:
            raise ValueError(f"builtin {self.kind}: format {f!r} needs {n} %s placeholders")
        return Builtin(self.kind, f, p, sig, tmpl, key=keyt, **self.flags)

    def _default(self):
        if self.needs_key:
            raise TypeError(f"B.{self.kind} needs its key type: B.{self.kind}(key=int)")
        if self.needs_type:
            raise TypeError(f"B.{self.kind} needs a type: B.{self.kind}(int)")
        return self()


class _SigProbe:
    """Stand-in grammar used only to count builtin arguments before a grammar exists."""
    input = T.i64
    output = T.boolean


def _sig_x(g):
    return (g.input, [])


def _bool2(g):
    return (T.boolean, [T.boolean, T.boolean])


def _rec(g):
    return (g.output, [g.input])


def _lexrec(g, k):
    # Builtins::LexiconRecurse is Primitive<output_t, key_t, input_t>: the key is the first argument
    return (g.output, [k, g.input])


class builtins:
    """Fleet's builtins (src/VirtualMachine/Builtins.h). Never re-implemented in fleetpy.

    Default formats follow Fleet's own models ("flip()", "x", "and(%s,%s)", ...).
    """
    X = _BuiltinFactory("X", "x", _sig_x, "Builtins::X<{G}>")
    And = _BuiltinFactory("And", "and(%s,%s)", _bool2, "Builtins::And<{G}>")
    Or = _BuiltinFactory("Or", "or(%s,%s)", _bool2, "Builtins::Or<{G}>")
    Not = _BuiltinFactory("Not", "not(%s)", lambda g: (T.boolean, [T.boolean]), "Builtins::Not<{G}>")
    Implies = _BuiltinFactory("Implies", "implies(%s,%s)", _bool2, "Builtins::Implies<{G}>")
    Iff = _BuiltinFactory("Iff", "iff(%s,%s)", _bool2, "Builtins::Iff<{G}>")
    If = _BuiltinFactory("If", "if(%s,%s,%s)", lambda g, t: (t, [T.boolean, t, t]),
                         "Builtins::If<{G},{T}>", needs_type=True)

    # stochastic: these fork the virtual machine; they need a StochasticHypothesis
    Flip = _BuiltinFactory("Flip", "flip()", lambda g: (T.boolean, []), "Builtins::Flip<{G}>",
                           stochastic=True)
    FlipP = _BuiltinFactory("FlipP", "flipp(%s)", lambda g: (T.boolean, [T.f64]), "Builtins::FlipP<{G}>",
                            stochastic=True)
    SafeFlipP = _BuiltinFactory("SafeFlipP", "safe_flipp(%s)", lambda g: (T.boolean, [T.f64]),
                                "Builtins::SafeFlipP<{G}>", stochastic=True)
    Sample = _BuiltinFactory("Sample", "sample(%s)", lambda g, t: (t, [T.SetOf(t)]),
                             "Builtins::Sample<{G},{T}>", needs_type=True, stochastic=True)

    # recursion into the hypothesis itself (DeterministicHypothesis / StochasticHypothesis)
    Recurse = _BuiltinFactory("Recurse", "recurse(%s)", _rec, "Builtins::Recurse<{G}>",
                              recursion="plain")
    SafeRecurse = _BuiltinFactory("SafeRecurse", "safe_recurse(%s)", _rec, "Builtins::SafeRecurse<{G}>",
                                  recursion="plain", safe=True)
    MemRecurse = _BuiltinFactory("MemRecurse", "mem_recurse(%s)", _rec, "Builtins::MemRecurse<{G}>",
                                 recursion="plain", memo=True)
    SafeMemRecurse = _BuiltinFactory("SafeMemRecurse", "safe_mem_recurse(%s)", _rec,
                                     "Builtins::SafeMemRecurse<{G}>", recursion="plain", memo=True, safe=True)

    # recursion into a lexicon factor chosen by key (Lexicon only)
    LexiconRecurse = _BuiltinFactory("LexiconRecurse", "F(%s,%s)", _lexrec,
                                     "Builtins::LexiconRecurse<{G},{K}>", needs_key=True, recursion="lexicon")
    LexiconSafeRecurse = _BuiltinFactory("LexiconSafeRecurse", "safe_F(%s,%s)", _lexrec,
                                         "Builtins::LexiconSafeRecurse<{G},{K}>", needs_key=True,
                                         recursion="lexicon", safe=True)
    LexiconMemRecurse = _BuiltinFactory("LexiconMemRecurse", "mem_F(%s,%s)", _lexrec,
                                        "Builtins::LexiconMemRecurse<{G},{K}>", needs_key=True,
                                        recursion="lexicon", memo=True)
    LexiconSafeMemRecurse = _BuiltinFactory("LexiconSafeMemRecurse", "safe_mem_F(%s,%s)", _lexrec,
                                            "Builtins::LexiconSafeMemRecurse<{G},{K}>", needs_key=True,
                                            recursion="lexicon", memo=True, safe=True)


class Grammar:
    """A Fleet grammar: ``Grammar<input, output, types...>``."""

    def __init__(self, input, output, types=()):
        self.input = T.resolve(input)
        self.output = T.resolve(output)
        self.declared_types = [T.resolve(t) for t in types]
        self.rules = []  # Primitive | Builtin, in declaration order (= Grammar::add order)

    # ---- declaring rules ---------------------------------------------------------
    def primitive(self, fmt=None, *, p=1.0, cpp=None, backend="cpp", max_iters=1_000_000):
        """Decorator declaring a primitive. See the design doc for the three tiers.

        ``@g.primitive`` and ``@g.primitive(...)`` are both accepted.
        """
        if callable(fmt) and not isinstance(fmt, str):  # bare @g.primitive
            fn, fmt = fmt, None
            self._add_primitive(Primitive(fn, None, p, cpp, backend, max_iters))
            return fn

        def deco(fn):
            self._add_primitive(Primitive(fn, fmt, p, cpp, backend, max_iters))
            return fn
        return deco

    def _add_primitive(self, prim):
        prim.grammar = self
        self.rules.append(prim)

    def add_terminal(self, fmt: str, value, type=None, *, p: float = 1.0):
        """A constant rule (``Grammar::add_terminal``). The type is inferred for bool, int,
        float and str values; pass ``type=`` for anything else (e.g. fl.char, fl.i8)."""
        if type is None:
            if isinstance(value, bool):
                type = T.boolean
            elif isinstance(value, int):
                type = T.i64
            elif isinstance(value, float):
                type = T.f64
            elif isinstance(value, str):
                type = T.string
            else:
                raise TypeError(f"cannot infer the type of terminal {fmt!r}; pass type=")
        self.rules.append(Terminal(fmt, value, T.resolve(type), p))

    def add_builtin(self, *bs):
        for b in bs:
            if isinstance(b, _BuiltinFactory):
                b = b._default()
            if not isinstance(b, Builtin):
                raise TypeError(f"add_builtin expects fleetpy builtins, got {b!r}")
            self.rules.append(b)

    # ---- derived information ----------------------------------------------------
    def rule_signature(self, r):
        if isinstance(r, Primitive):
            return r.rettype, r.argtypes
        if isinstance(r, Terminal):
            return r.type, []
        return r.sig(self)

    def builtins_used(self):
        return [r for r in self.rules if isinstance(r, Builtin)]

    def all_types(self):
        """Nonterminal types, in a deterministic order: declared, input, output, then discovered."""
        seen, out = set(), []

        def push(t):
            if t.key not in seen:
                seen.add(t.key)
                out.append(t)
        for t in self.declared_types:
            push(t)
        push(self.input)
        push(self.output)
        for r in self.rules:
            ret, args = self.rule_signature(r)
            push(ret)
            for a in args:
                push(a)
        return out

    def structs(self):
        """Every struct/newtype the grammar needs, dependencies first, no duplicates."""
        seen, out = set(), []
        for t in self.all_types():
            for s in t.structs():
                if s.key not in seen:
                    seen.add(s.key)
                    out.append(s)
        return out

    def check(self):
        """Static checks run before code generation. Raises on errors; warns on hazards."""
        if not self.rules:
            raise ValueError("grammar has no rules")
        # A format may be reused only for a different return type (as Fleet's Number model does
        # with if(%s,%s,%s) for sets and words): Fleet's serialized form and fleetpy's typed
        # parser identify a rule by (nonterminal, format). Within one type it must be unique.
        fmts = {}
        for r in self.rules:
            k = (self.rule_signature(r)[0].key, r.fmt)
            if k in fmts:
                raise ValueError(
                    f"two rules of type {k[0]} share the format {r.fmt!r} ({fmts[k]!r} and {r!r}); "
                    f"pass fmt= to one of them")
            fmts[k] = r
        for r in self.rules:
            if isinstance(r, Builtin) and r.safe:
                if not isinstance(self.input, (T._Str, T.ListOf, T.SetOf)):
                    raise TypeError(f"{r.kind} needs an input type with size() (str, list or frozenset); "
                                    f"the grammar's input is {self.input.key}")
        names = {}
        for r in self.rules:
            if isinstance(r, Primitive):
                if r.name in names:
                    raise ValueError(f"two primitives are named {r.name!r}")
                names[r.name] = r
        # Every nonterminal must be producible, otherwise sampling it fails at runtime.
        produced = {self.rule_signature(r)[0].key for r in self.rules}
        needed = {self.output.key}
        for r in self.rules:
            for a in self.rule_signature(r)[1]:
                needed.add(a.key)
        missing = needed - produced
        if missing:
            raise ValueError(f"no rule produces type(s) {sorted(missing)}, which are needed "
                             f"(as the output or as an argument)")
        # Every needed type must be able to produce a *finite* tree: least fixpoint of
        # "some rule of type t has all argument types finite". Otherwise sampling never ends.
        finite = set()
        changed = True
        while changed:
            changed = False
            for r in self.rules:
                ret, args = self.rule_signature(r)
                if ret.key not in finite and all(a.key in finite for a in args):
                    finite.add(ret.key)
                    changed = True
        for k in sorted(needed):
            if k not in finite:
                raise ValueError(f"type {k} cannot produce any finite program (every rule for it "
                                 f"needs an argument of a type with no finite program)")
        nonc = [r.fmt for r in self.rules if not is_canonical_format(r.fmt)]
        self.parse_ok = not nonc
        self.noncanonical = nonc
        return self

    def spec_dict(self):
        """A canonical, JSON-serializable description used in the build hash."""
        out = {"input": self.input.key, "output": self.output.key,
               "types": [t.key for t in self.all_types()], "rules": []}
        for r in self.rules:
            if isinstance(r, Primitive):
                out["rules"].append({
                    "kind": "primitive", "name": r.name, "fmt": r.fmt, "p": r.p, "tier": r.tier,
                    "args": [[n, t.key] for n, t in zip(r.argnames, r.argtypes)],
                    "ret": r.rettype.key, "cpp": r.cpp, "max_iters": r.max_iters,
                    "src": _source(r.fn) if r.tier == 2 else None,
                    "pyid": f"{r.fn.__module__}.{r.fn.__qualname__}" if r.tier == 3 else None,
                })
            elif isinstance(r, Terminal):
                from .codegen import cpp_literal
                out["rules"].append({"kind": "terminal", "fmt": r.fmt, "p": r.p, "type": r.type.key,
                                     "value": cpp_literal(r.type, r.value)})
            else:
                out["rules"].append({"kind": "builtin", "builtin": r.kind, "fmt": r.fmt, "p": r.p,
                                     "cpp": r.cpp_template})
        return out


def _source(fn):
    try:
        return inspect.getsource(fn)
    except (OSError, TypeError):
        return None
