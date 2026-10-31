"""
Declaring a model, and resolving the declaration into a ``ModelSpec``.

A model is declared as a class:

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.noisy_exact()   # or an @fl.likelihood method
        proposal = fl.proposals.regenerate()        # optional
        vm = fl.VMLimits(max_recurse=10)            # optional
        @fl.prior                                   # optional
        def prior(self) -> float: ...

The class is only a declaration. ``specify(H)`` checks it as a whole and resolves it into a
``ModelSpec``: plain data with every Python body already compiled to C++. Code generation
and the compiled ``Model`` read the spec, never the class.
"""

from __future__ import annotations

import dataclasses
import json
import typing

from . import ftypes as T
from .grammar import Grammar
from .likelihoods import Likelihood
from .rules import Builtin, Terminal

DETERMINISTIC, STOCHASTIC, LEXICON = "deterministic", "stochastic", "lexicon"


class VMLimits:
    """Fleet's VirtualMachineControl settings. They are part of a model's semantics: they
    decide which execution paths a stochastic program's distribution includes and when
    recursion is cut off. Defaults are Fleet's.

      max_recurse      recursive calls per execution path (more -> error)
      max_run_program  instructions per execution path (more -> error)
      max_steps        VirtualMachinePool steps per call (stochastic)
      max_outputs      distinct outputs per call (stochastic)
      min_lp           execution paths below this log probability are pruned (stochastic)
    """

    def __init__(self, max_recurse=64, max_run_program=10000, max_steps=512, max_outputs=512, min_lp=-10.0):
        for n, v in (("max_recurse", max_recurse), ("max_run_program", max_run_program),
                     ("max_steps", max_steps), ("max_outputs", max_outputs)):
            if not (isinstance(v, int) and v >= 1):
                raise ValueError(f"{n} must be an int >= 1")
        if not isinstance(min_lp, (int, float)) or min_lp > 0:
            raise ValueError("min_lp must be a number <= 0")
        self.max_recurse, self.max_run_program = max_recurse, max_run_program
        self.max_steps, self.max_outputs, self.min_lp = max_steps, max_outputs, float(min_lp)

    def spec(self):
        return dict(vars(self))


@dataclasses.dataclass(frozen=True)
class Proposal:
    """A proposal kernel for a LOTHypothesis (each factor of a Lexicon included). ``body``
    is C++ assigning ``std::optional<std::pair<Node,double>> p`` from ``value`` (the current
    tree) and ``&grammar``; an empty ``p`` is a failed (rejected) proposal."""
    body: str
    name: str

    def spec(self):
        return {"name": self.name, "body": self.body}


class proposals:
    """Proposal kernels built from Fleet's Proposals:: functions."""

    @staticmethod
    def regenerate() -> Proposal:
        """Fleet's default (LOTHypothesis::propose): regenerate a subtree from the prior."""
        return Proposal("p = Proposals::regenerate(&grammar, value);", "regenerate")

    @staticmethod
    def regenerate_insert_delete(regenerate: float = 0.75) -> Proposal:
        """With probability ``regenerate``, regenerate; otherwise insert or delete a subtree
        with equal probability: the draw sequence of Models/FormalLanguageTheory-Simple
        (flip(r), then flip())."""
        if not 0.0 < regenerate <= 1.0:
            raise ValueError("regenerate must be in (0, 1]")
        return Proposal(f"if(flip({regenerate!r})) p = Proposals::regenerate(&grammar, value);\n"
                        f"else p = flip() ? Proposals::insert_tree(&grammar, value) : "
                        f"Proposals::delete_tree(&grammar, value);",
                        f"regenerate_insert_delete({regenerate!r})")

    @staticmethod
    def cpp(body: str) -> Proposal:
        if "p =" not in body and "p=" not in body:
            raise ValueError("a proposal body must assign p")
        return Proposal(body, "cpp")


# ---- Python methods compiled to C++ ---------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class PythonLikelihood:
    """Set by @fl.likelihood; compiled when the model is built."""
    fn: typing.Callable
    nonpositive: bool
    total: bool


def likelihood(fn=None, *, nonpositive: bool = False, total: bool = False):
    """Decorator for a likelihood written in Python (compiled to C++).

    Per datum (default): ``def f(self, di) -> float``.
    Whole dataset (total=True): ``def f(self, data) -> float`` with len(data) and data[i];
    this replaces Fleet's compute_likelihood (as Models/Number-Simple does).

    ``self.call(x)`` runs a deterministic hypothesis; if the program is undefined on x, the
    datum's (with total=True, the dataset's) log likelihood is -inf. An uncaught Undefined
    anywhere in the body means the same. ``self.call(x, default)`` returns ``default``
    instead (Fleet's call(x, err)). ``self.logprob(x, y)`` is log P(y | x) for a stochastic
    hypothesis.
    """
    def deco(f):
        return PythonLikelihood(f, nonpositive, total)
    return deco(fn) if fn is not None else deco


@dataclasses.dataclass(frozen=True)
class PythonPrior:
    fn: typing.Callable


def prior(fn):
    """Decorator for a prior written in Python, ``def p(self) -> float``, replacing
    compute_prior. It may call self.grammar_prior() (Fleet's PCFG prior: the default),
    self.recursion_count() and self.nodes(). As in Fleet, a program with more than
    MAX_NODES nodes gets -inf before this runs."""
    return PythonPrior(fn)


# ---- the declaration classes ----------------------------------------------------------------

_RESERVED_CLASS_NAMES = ("FPInner", "MyGrammar")


class _Declaration:
    kind = None
    stochastic = False
    _python_prior = None

    def __init_subclass__(cls, grammar: Grammar = None, **kw):
        super().__init_subclass__(**kw)
        if "kind" in cls.__dict__:   # one of fleetpy's own base classes
            return
        if not isinstance(grammar, Grammar):
            raise TypeError(f"{cls.__name__}: pass the grammar, e.g. "
                            f"class {cls.__name__}(fl.DeterministicHypothesis, grammar=g)")
        T.check_identifier(cls.__name__, "hypothesis class name")
        if cls.__name__ in _RESERVED_CLASS_NAMES:
            raise TypeError(f"{cls.__name__} is reserved for generated code")
        cls.grammar = grammar
        # at most one of each way to give the likelihood and the prior
        py_lik = [v for v in cls.__dict__.values() if isinstance(v, PythonLikelihood)]
        py_prior = [v for v in cls.__dict__.values() if isinstance(v, PythonPrior)]
        if len(py_lik) > 1 or len(py_prior) > 1:
            raise TypeError(f"{cls.__name__}: more than one @fl.likelihood or @fl.prior method")
        if py_lik and isinstance(cls.__dict__.get("likelihood"), Likelihood):
            raise TypeError(f"{cls.__name__}: both `likelihood = ...` and an @fl.likelihood method")
        if py_prior and cls.__dict__.get("prior_cpp") is not None:
            raise TypeError(f"{cls.__name__}: both prior_cpp and an @fl.prior method")
        if py_lik:
            cls.likelihood = py_lik[0]
        if py_prior:
            cls._python_prior = py_prior[0]
        if not isinstance(getattr(cls, "likelihood", None), (Likelihood, PythonLikelihood)):
            raise TypeError(f"{cls.__name__}: set `likelihood = ...` to a fleetpy likelihood "
                            f"(fl.likelihoods.noisy_exact(), fl.likelihoods.cpp(...), or @fl.likelihood)")
        pc = getattr(cls, "prior_cpp", None)
        if pc is not None and (not isinstance(pc, str) or "return" not in pc):
            raise TypeError("prior_cpp must be a C++ body with a return statement")
        if getattr(cls, "proposal", None) is not None and not isinstance(cls.proposal, Proposal):
            raise TypeError("proposal must be a fleetpy proposal, e.g. fl.proposals.regenerate_insert_delete()")
        if getattr(cls, "vm", None) is not None and not isinstance(cls.vm, VMLimits):
            raise TypeError("vm must be fl.VMLimits(...)")


class DeterministicHypothesis(_Declaration):
    """Generates ``class H final : public DeterministicLOTHypothesis<H, in, out, MyGrammar, &grammar>``."""
    kind = DETERMINISTIC


class StochasticHypothesis(_Declaration):
    """Generates ``class H final : public StochasticLOTHypothesis<H, in, out, MyGrammar, &grammar>``.

    A call runs every execution path (Flip, FlipP, Sample fork the VM) and returns Fleet's
    DiscreteDistribution over outputs: unnormalized log masses, missing whatever mass was
    pruned (VMLimits.min_lp, max_steps, max_outputs) or ended in an error.
    """
    kind = STOCHASTIC
    stochastic = True


class Lexicon(_Declaration):
    """Generates Fleet's ``Lexicon<H, key_t, FPInner, in, out>`` with one factor per key.

        class L(fl.Lexicon, grammar=g, keys=[0, 1], main=1, stochastic=False):
            likelihood = ...

    ``main`` is the factor a call runs; factors call each other through the grammar's
    B.Lexicon*Recurse(key=...) builtins. Every factor is a Deterministic (or, with
    stochastic=True, Stochastic) LOTHypothesis over the same grammar. ``proposal`` applies
    to each factor; Fleet's Lexicon proposes to a random nonempty subset of factors. The
    prior is Fleet's: log(1/2) * (factors + 1) + the factors' priors.

    require_all_called=True gives a likelihood of -inf unless every factor was used while
    computing it (as in Models/FormalLanguageTheory-Complex).

    If no grammar rule produces the key type, the built model's grammar gets one terminal
    per key (printed as the key for int keys, in single quotes for str keys). The key type
    must not be used by any other rule.
    """
    kind = LEXICON

    def __init_subclass__(cls, keys=None, main=None, stochastic: bool = False,
                          require_all_called: bool = False, **kw):
        super().__init_subclass__(**kw)
        if "kind" in cls.__dict__:
            return
        if not keys:
            raise TypeError(f"{cls.__name__}: give keys=[...] (one factor per key)")
        keys = list(keys)
        if len(set(keys)) != len(keys):
            raise ValueError("lexicon keys must be distinct")
        if all(T.is_int_value(k) for k in keys):
            cls.key_type = T.i32
        elif all(isinstance(k, str) for k in keys):
            cls.key_type = T.string
        else:
            raise TypeError("lexicon keys must be all ints or all strs")
        for k in keys:
            cls.key_type.to_cpp(k)
        if main is None or main not in keys:
            raise ValueError(f"main= must be one of the keys {keys}")
        cls.keys = sorted(keys)   # Fleet keeps factors in a std::map: sorted by key
        cls.main = main
        cls.stochastic = bool(stochastic)
        cls.require_all_called = bool(require_all_called)


# ---- the resolved model ---------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Code:
    """C++ produced from Python (or written by the user), with the types it uses."""
    body: str
    transpiled: bool = False
    pyfn: typing.Callable = None
    types: tuple = ()


@dataclasses.dataclass(frozen=True)
class LexiconSpec:
    keys: list
    main: object
    key_type: T.FType
    require_all_called: bool


@dataclasses.dataclass(frozen=True)
class ModelSpec:
    name: str
    kind: str                    # DETERMINISTIC | STOCHASTIC | LEXICON
    stochastic: bool             # do programs output distributions (Stochastic*, or a stochastic Lexicon)?
    grammar: Grammar             # the grammar as built (a Lexicon's may have key terminals added)
    likelihood: Likelihood
    prior: Code                  # None: Fleet's prior
    proposal: Proposal           # None: Fleet's default
    vm: VMLimits
    lexicon: LexiconSpec         # None unless kind == LEXICON
    primitives: dict             # name -> Code, for tier-2 primitives

    @property
    def is_lexicon(self):
        return self.kind == LEXICON

    def types(self) -> list:
        """Every type the generated code uses: the grammar's, then those used only inside bodies."""
        extra = [t for c in (self.likelihood, self.prior, *self.primitives.values()) if c is not None
                 for t in c.types]
        return self.grammar.types() + extra

    def json(self) -> str:
        return json.dumps({
            "kind": self.kind, "name": self.name, "stochastic": self.stochastic,
            "grammar": self.grammar.spec(), "likelihood": self.likelihood.spec(),
            "prior": self.prior.body if self.prior else None,
            "proposal": self.proposal.spec() if self.proposal else None, "vm": self.vm.spec(),
            "lexicon": None if self.lexicon is None else {
                "keys": self.lexicon.keys, "main": self.lexicon.main,
                "key_type": self.lexicon.key_type.key, "require_all_called": self.lexicon.require_all_called},
        }, sort_keys=True)


def is_declaration(H) -> bool:
    return isinstance(H, type) and issubclass(H, _Declaration) and "kind" not in H.__dict__


def specify(H) -> ModelSpec:
    """Check a model declaration as a whole and resolve it into a ModelSpec."""
    from . import transpile
    if not is_declaration(H):
        raise TypeError("build() expects a model class: a subclass of fl.DeterministicHypothesis, "
                        "fl.StochasticHypothesis or fl.Lexicon")
    g = H.grammar
    lexicon = LexiconSpec(H.keys, H.main, H.key_type, H.require_all_called) if H.kind == LEXICON else None
    _check_builtins(H, g, lexicon)    # before the grammar check: these errors explain more
    if lexicon is not None:
        g = _lexicon_grammar(g, lexicon)
    g.check()

    lik = H.likelihood
    if isinstance(lik, PythonLikelihood):
        lik = transpile.likelihood(lik, g, stochastic=H.stochastic)
    if lik.needs == "dist" and not H.stochastic:
        raise TypeError(f"{lik.name} scores an output distribution; it needs a stochastic hypothesis "
                        f"(fl.StochasticHypothesis or fl.Lexicon(..., stochastic=True))")
    if lik.needs == "value" and H.stochastic:
        raise TypeError(f"{lik.name} scores a single output; a stochastic hypothesis outputs a "
                        f"distribution (use fl.likelihoods.stochastic_exact(), string_edit(...), or cpp)")
    if lik.scope == "total" and lexicon is not None:
        raise ValueError("whole-dataset likelihoods are not supported for a Lexicon yet")

    pr = None
    if H._python_prior is not None:
        pr = transpile.prior(H._python_prior.fn, g)
    elif getattr(H, "prior_cpp", None) is not None:
        pr = Code(H.prior_cpp)
    if pr is not None and lexicon is not None:
        raise TypeError("a custom prior is not supported for a Lexicon (its prior is Fleet's Lexicon prior)")

    prims = {r.name: transpile.primitive(r, g) for r in g.primitives(tier=2)}
    return ModelSpec(name=H.__name__, kind=H.kind, stochastic=H.stochastic, grammar=g, likelihood=lik,
                     prior=pr, proposal=getattr(H, "proposal", None), vm=getattr(H, "vm", None) or VMLimits(),
                     lexicon=lexicon, primitives=prims)


def _check_builtins(H, g, lexicon):
    for b in g.builtins():
        s = b.builtin
        if s.stochastic and not H.stochastic:
            raise TypeError(f"{s.name} forks the virtual machine and needs a stochastic hypothesis "
                            f"(a DeterministicLOTHypothesis has no VirtualMachinePool)")
        if s.recursion == "lexicon" and lexicon is None:
            raise TypeError(f"{s.name} dispatches to lexicon factors; it needs fl.Lexicon")
        if s.recursion == "self" and lexicon is not None:
            raise TypeError(f"{s.name} inside a Lexicon would call Lexicon::push_program without a key, "
                            f"which Fleet does not implement; use the Lexicon* recursions")
        if s.recursion == "lexicon" and b.param != lexicon.key_type:
            raise TypeError(f"{s.name} uses key type {b.param.key} but the lexicon's keys are "
                            f"{lexicon.key_type.key}")


def _lexicon_grammar(g: Grammar, lex: LexiconSpec) -> Grammar:
    """The grammar a Lexicon is built with: the user's, plus a terminal per key if no rule
    produces keys."""
    kt = lex.key_type
    if not any(b.builtin.recursion == "lexicon" for b in g.builtins()):
        return g
    users = [r for r in g.rules if kt in r.args
             and not (isinstance(r, Builtin) and r.builtin.recursion == "lexicon")]
    if users:
        raise TypeError(f"the key type {kt.key} is also an argument of {users[0]!r}; keys and other "
                        f"values of that type would mix. Use a different key type.")
    producers = [r for r in g.rules if r.ret == kt]
    if not producers:
        return g.extended(Terminal(str(k) if isinstance(k, int) else f"'{k}'", k, kt) for k in lex.keys)
    bad = [r for r in producers if not (isinstance(r, Terminal) and r.value in lex.keys)]
    if bad:
        raise TypeError(f"{bad[0]!r} produces a key that is not one of the lexicon's keys {lex.keys}")
    return g
