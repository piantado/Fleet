"""
A grammar: rules over typed nonterminals (Fleet's ``Grammar<input, output, types...>``).

Declaring rules only records them; ``fleetpy.build`` turns the grammar into C++.
"""

from __future__ import annotations

from . import ftypes as T
from .rules import Builtin, BuiltinFactory, Primitive, Terminal, is_canonical_format


class Grammar:

    def __init__(self, input, output, types=()):
        self.input = T.resolve(input)
        self.output = T.resolve(output)
        self.declared_types = [T.resolve(t) for t in types]
        self.rules = []   # in declaration order, which is Grammar::add order

    # ---- declaring rules -------------------------------------------------------------
    def primitive(self, fmt=None, *, p=1.0, cpp=None, backend="cpp", max_iters=1_000_000):
        """Decorator declaring a primitive; ``@g.primitive`` and ``@g.primitive(...)`` both work."""
        if callable(fmt):   # bare @g.primitive
            self.rules.append(Primitive(fmt, p=p, cpp=cpp, backend=backend, max_iters=max_iters))
            return fmt

        def deco(fn):
            self.rules.append(Primitive(fn, fmt, p=p, cpp=cpp, backend=backend, max_iters=max_iters))
            return fn
        return deco

    def add_terminal(self, fmt: str, value, type=None, *, p: float = 1.0):
        """A constant rule (``Grammar::add_terminal``). The type is inferred for bool, int,
        float and str values; pass ``type=`` otherwise (e.g. fl.char, fl.i8)."""
        self.rules.append(Terminal(fmt, value, T.infer(value) if type is None else type, p))

    def add_builtin(self, *builtins):
        for b in builtins:
            if isinstance(b, BuiltinFactory):
                b = b.default()
            if not isinstance(b, Builtin):
                raise TypeError(f"add_builtin expects fleetpy builtins, got {b!r}")
            self.rules.append(b.bind(self))

    def extended(self, rules) -> "Grammar":
        """A copy with ``rules`` appended (the user's grammar is never changed by a build)."""
        g = Grammar(self.input, self.output, self.declared_types)
        g.rules = list(self.rules) + list(rules)
        return g

    # ---- derived information -------------------------------------------------------------
    def types(self) -> list:
        """The nonterminal types, in Fleet's order: declared, input, output, then discovered."""
        out = {}
        for t in [*self.declared_types, self.input, self.output,
                  *(t for r in self.rules for t in (r.ret, *r.args))]:
            out.setdefault(t.key, t)
        return list(out.values())

    def nt(self, t) -> int:
        """The index of type ``t`` as a nonterminal (Fleet's ``nt<T>()``)."""
        return [x.key for x in self.types()].index(t.key)

    def primitives(self, tier=None) -> list:
        return [r for r in self.rules if isinstance(r, Primitive) and (tier is None or r.tier == tier)]

    def builtins(self) -> list:
        return [r for r in self.rules if isinstance(r, Builtin)]

    def rule(self, fmt: str, ret: T.FType):
        """The rule printed as ``fmt`` that produces ``ret``; (type, format) is unique."""
        for r in self.rules:
            if r.fmt == fmt and r.ret == ret:
                return r
        others = [r.ret.key for r in self.rules if r.fmt == fmt]
        if others:
            raise TypeError(f"{fmt} returns {others[0]} but {ret.key} is needed here")
        raise ValueError(f"no rule with format {fmt!r}")

    @property
    def noncanonical(self) -> list:
        return [r.fmt for r in self.rules if not is_canonical_format(r.fmt)]

    @property
    def canonical(self) -> bool:
        """True if printed programs can be parsed (see ``Model.parse``)."""
        return not self.noncanonical

    # ---- static checks ---------------------------------------------------------------------
    def check(self):
        """Raise if sampling from this grammar could fail or its programs be ambiguous."""
        if not self.rules:
            raise ValueError("grammar has no rules")
        # Fleet's serialized form and fleetpy's typed parser identify a rule by (type, format).
        # A format may be reused for another type (Fleet's Number model does this with if).
        seen = {}
        for r in self.rules:
            k = (r.ret.key, r.fmt)
            if k in seen:
                raise ValueError(f"two rules of type {k[0]} share the format {r.fmt!r} "
                                 f"({seen[k]!r} and {r!r}); pass fmt= to one of them")
            seen[k] = r
        names = {}
        for r in self.primitives():
            if r.name in names:
                raise ValueError(f"two primitives are named {r.name!r}")
            names[r.name] = r
        for r in self.builtins():
            if r.builtin.safe and self.input.category not in ("str", "list", "set"):
                raise TypeError(f"{r.builtin.name} needs an input type with size() (str, list or "
                                f"frozenset); the grammar's input is {self.input.key}")
        produced = {r.ret.key for r in self.rules}
        needed = {self.output.key} | {a.key for r in self.rules for a in r.args}
        missing = needed - produced
        if missing:
            raise ValueError(f"no rule produces type(s) {sorted(missing)}, which are needed "
                             f"(as the output or as an argument)")
        # Every needed type must have a *finite* program: the least fixpoint of "some rule
        # of type t has only finite argument types". Otherwise sampling never ends.
        finite, changed = set(), True
        while changed:
            changed = False
            for r in self.rules:
                if r.ret.key not in finite and all(a.key in finite for a in r.args):
                    finite.add(r.ret.key)
                    changed = True
        for k in sorted(needed - finite):
            raise ValueError(f"type {k} cannot produce any finite program (every rule for it "
                             f"needs an argument of a type with no finite program)")
        return self

    def spec(self) -> dict:
        """A canonical, JSON-serializable description (part of the build hash)."""
        return {"input": self.input.key, "output": self.output.key,
                "types": [t.key for t in self.types()], "rules": [r.spec() for r in self.rules]}
