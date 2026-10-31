"""
Hypothesis and inference specifications.
"""

from __future__ import annotations

import re

from . import ftypes as T
from .grammar import Grammar
from .likelihoods import Likelihood


class VMLimits:
    """Fleet's VirtualMachineControl settings, which are part of a model's semantics (they
    decide which execution paths a stochastic program's output distribution includes, and
    when recursion is cut off). Defaults are Fleet's.

      max_recurse      -- recursive calls per execution path (more -> error)
      max_run_program  -- instructions per execution path (more -> error)
      max_steps        -- VirtualMachinePool steps per call (stochastic)
      max_outputs      -- distinct outputs per call (stochastic)
      min_lp           -- execution paths below this log probability are pruned (stochastic)
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

    def spec_dict(self):
        return dict(vars(self))


class Proposal:
    """A proposal kernel for a LOTHypothesis (the inner hypotheses of a Lexicon included).

    ``body`` is C++ that assigns ``std::optional<std::pair<Node,double>> p`` from ``value``
    (the current tree) and ``&grammar``; empty ``p`` means the proposal failed (a rejected step).
    """

    def __init__(self, body: str, name: str):
        self.body, self.name = body, name

    def spec_dict(self):
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
        with equal probability. The exact random-draw sequence of
        Models/FormalLanguageTheory-Simple (flip(r), then flip())."""
        if not (0.0 < regenerate <= 1.0):
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


class _HypothesisBase:
    _kind = None

    def __init_subclass__(cls, grammar: Grammar = None, **kw):
        super().__init_subclass__(**kw)
        if "_kind" in cls.__dict__:  # a fleetpy base class, not a user model
            return
        if grammar is None:
            raise TypeError(f"{cls.__name__}: pass the grammar, e.g. class {cls.__name__}"
                            f"(fl.DeterministicHypothesis, grammar=g)")
        if not isinstance(grammar, Grammar):
            raise TypeError("grammar= must be a fleetpy Grammar")
        T.check_identifier(cls.__name__, "hypothesis class name")
        if cls.__name__ in ("FPInner", "MyGrammar"):
            raise TypeError(f"{cls.__name__} is reserved for generated code")
        cls.grammar = grammar
        from .transpile import _PendingLikelihood, _PendingPrior
        pp = [v for v in cls.__dict__.values() if isinstance(v, _PendingPrior)]
        if len(pp) > 1:
            raise TypeError(f"{cls.__name__}: more than one @fl.prior method")
        if pp and cls.__dict__.get("prior_cpp") is not None:
            raise TypeError(f"{cls.__name__}: both prior_cpp and an @fl.prior method")
        cls._pending_prior = pp[0] if pp else None
        pend = [v for v in cls.__dict__.values() if isinstance(v, _PendingLikelihood)]
        if len(pend) > 1:
            raise TypeError(f"{cls.__name__}: more than one @fl.likelihood method")
        if pend:
            if isinstance(cls.__dict__.get("likelihood"), Likelihood):
                raise TypeError(f"{cls.__name__}: both `likelihood = ...` and an @fl.likelihood method")
            cls.likelihood = pend[0]  # transpiled at build time, when the grammar is complete
        lik = getattr(cls, "likelihood", None)
        if not isinstance(lik, (Likelihood, _PendingLikelihood)):
            raise TypeError(f"{cls.__name__}: set `likelihood = ...` to a fleetpy likelihood "
                            f"(fl.likelihoods.noisy_exact(), fl.likelihoods.cpp(...), or @fl.likelihood)")
        prior = getattr(cls, "prior_cpp", None)
        if prior is not None and (not isinstance(prior, str) or "return" not in prior):
            raise TypeError("prior_cpp must be a C++ body with a return statement")
        prop = getattr(cls, "proposal", None)
        if prop is not None and not isinstance(prop, Proposal):
            raise TypeError("proposal must be a fleetpy proposal, e.g. fl.proposals.regenerate_insert_delete()")
        vm = getattr(cls, "vm", None)
        if vm is not None and not isinstance(vm, VMLimits):
            raise TypeError("vm must be fl.VMLimits(...)")

    # which flavor of LOTHypothesis computes a program's output
    stochastic = False

    @classmethod
    def vm_limits(cls) -> VMLimits:
        return getattr(cls, "vm", None) or VMLimits()

    @classmethod
    def finalize(cls):
        """Transpile a pending @fl.likelihood and check the model as a whole (needs the finished grammar)."""
        from .transpile import _PendingLikelihood, transpile_likelihood
        if isinstance(cls.likelihood, _PendingLikelihood):
            cls._pending_likelihood = cls.likelihood
        if getattr(cls, "_pending_likelihood", None) is not None:
            cls.likelihood = transpile_likelihood(cls._pending_likelihood, cls.grammar, cls)
        if getattr(cls, "_pending_prior", None) is not None:
            if cls._kind == "lexicon":
                raise TypeError("@fl.prior is not supported for a Lexicon (its prior is Fleet's Lexicon prior)")
            from .transpile import transpile_prior
            cls.prior_cpp = transpile_prior(cls._pending_prior, cls.grammar)
            cls._prior_transpiled = True
        cls._check_model()

    @classmethod
    def _check_model(cls):
        g = cls.grammar
        need = cls.likelihood.needs
        if need == "dist" and not cls.stochastic:
            raise TypeError(f"{cls.likelihood.name} scores an output distribution; it needs a stochastic "
                            f"hypothesis (fl.StochasticHypothesis or fl.Lexicon(..., stochastic=True))")
        if need == "value" and cls.stochastic:
            raise TypeError(f"{cls.likelihood.name} scores a single output; a stochastic hypothesis outputs a "
                            f"distribution (use fl.likelihoods.stochastic_exact(), string_edit(...), or cpp)")
        for b in g.builtins_used():
            if b.stochastic and not cls.stochastic:
                raise TypeError(f"{b.kind} forks the virtual machine and needs a stochastic hypothesis "
                                f"(a DeterministicLOTHypothesis has no VirtualMachinePool)")
            if b.recursion == "lexicon" and cls._kind != "lexicon":
                raise TypeError(f"{b.kind} dispatches to lexicon factors; it needs fl.Lexicon")
            if b.recursion == "plain" and cls._kind == "lexicon":
                raise TypeError(f"{b.kind} inside a Lexicon would call Lexicon::push_program without a key, "
                                f"which Fleet does not implement; use the Lexicon* recursions")

    @classmethod
    def spec_dict(cls):
        return {"kind": cls._kind, "name": cls.__name__, "grammar": cls.grammar.spec_dict(),
                "stochastic": cls.stochastic, "likelihood": cls.likelihood.spec_dict(),
                "prior_cpp": getattr(cls, "prior_cpp", None),
                "proposal": (cls.proposal.spec_dict() if getattr(cls, "proposal", None) else None),
                "vm": cls.vm_limits().spec_dict()}


class DeterministicHypothesis(_HypothesisBase):
    """Generates ``class H final : public DeterministicLOTHypothesis<H, in, out, MyGrammar, &grammar>``."""
    _kind = "deterministic"


class StochasticHypothesis(_HypothesisBase):
    """Generates ``class H final : public StochasticLOTHypothesis<H, in, out, MyGrammar, &grammar>``.

    A call runs every execution path (Flip, FlipP, Sample fork the VM) and returns Fleet's
    DiscreteDistribution over outputs: unnormalized log masses, missing whatever mass was
    pruned (VMLimits.min_lp, max_steps, max_outputs) or ended in an error.
    """
    _kind = "stochastic"
    stochastic = True


class Lexicon(_HypothesisBase):
    """Generates Fleet's ``Lexicon<H, key_t, FPInner, in, out>`` with one factor per key.

        class L(fl.Lexicon, grammar=g, keys=[0, 1], main=1, stochastic=False):
            likelihood = ...

    ``main`` is the factor a call runs; factors call each other through the grammar's
    B.Lexicon*Recurse(key=...) builtins. Every factor is a Deterministic (or, with
    stochastic=True, Stochastic) LOTHypothesis over the same grammar. ``proposal`` applies to
    each factor; Fleet's Lexicon proposes to a random nonempty subset of factors. The prior is
    Fleet's: log(1/2) * (factors + 1) + the factors' priors.

    require_all_called=True gives a likelihood of -inf unless every factor was used while
    computing it (as in Models/FormalLanguageTheory-Complex).

    If no grammar rule produces the key type, one terminal per key is added (format: the key
    for int keys, the key in single quotes for str keys); otherwise the grammar's own key
    rules are used. The key type must not be used by any other rule.
    """
    _kind = "lexicon"

    def __init_subclass__(cls, keys=None, main=None, stochastic: bool = False,
                          require_all_called: bool = False, **kw):
        super().__init_subclass__(**kw)
        if "_kind" in cls.__dict__:
            return
        if not keys:
            raise TypeError(f"{cls.__name__}: give keys=[...] (one factor per key)")
        keys = list(keys)
        if len(set(keys)) != len(keys):
            raise ValueError("lexicon keys must be distinct")
        if all(isinstance(k, int) and not isinstance(k, bool) for k in keys):
            cls.key_type = T.i32
        elif all(isinstance(k, str) for k in keys):
            cls.key_type = T.string
        else:
            raise TypeError("lexicon keys must be all ints or all strs")
        for k in keys:
            cls.key_type.to_cpp(k, None)
        if main is None or main not in keys:
            raise ValueError(f"main= must be one of the keys {keys}")
        cls.keys = sorted(keys)  # Fleet stores factors in a std::map: sorted by key
        cls.main = main
        cls.stochastic = bool(stochastic)
        cls.require_all_called = bool(require_all_called)

    @classmethod
    def _check_model(cls):
        super()._check_model()
        g = cls.grammar
        from .grammar import Builtin, Terminal
        lex = [b for b in g.builtins_used() if b.recursion == "lexicon"]
        for b in lex:
            if b.key.key != cls.key_type.key:
                raise TypeError(f"{b.kind} uses key type {b.key.key} but the lexicon's keys are {cls.key_type.key}")
        if not lex:
            return
        kt = cls.key_type
        uses = [r for r in g.rules if not (isinstance(r, Builtin) and r.recursion == "lexicon")
                and kt.key in [t.key for t in g.rule_signature(r)[1]]]
        if uses:
            raise TypeError(f"the key type {kt.key} is also an argument of {uses[0]!r}; keys and other "
                            f"values of that type would mix. Use a different key type.")
        producers = [r for r in g.rules if g.rule_signature(r)[0].key == kt.key]
        if not producers and not getattr(g, "_auto_keys", None):
            for k in cls.keys:
                fmt = str(k) if isinstance(k, int) else "'" + k + "'"
                g.add_terminal(fmt, k, kt)
            g._auto_keys = cls.keys
        else:
            bad = [r for r in producers if not (isinstance(r, Terminal) and r.value in cls.keys)]
            if bad:
                raise TypeError(f"{bad[0]!r} produces a key that is not one of the lexicon's keys {cls.keys}")

    @classmethod
    def spec_dict(cls):
        d = super().spec_dict()
        d.update({"keys": cls.keys, "main": cls.main, "key_type": cls.key_type.key,
                  "require_all_called": cls.require_all_called})
        return d


# ---------------------------------------------------------------------------------------
# Inference
# ---------------------------------------------------------------------------------------

_TIME = re.compile(r"^\s*([0-9]*\.?[0-9]+)\s*([smhd]?)\s*$")


def _parse_time_ms(t) -> int:
    """'30s', '2m', '1.5h', 10 (seconds) -> milliseconds. Mirrors Fleet's smhd units."""
    if t is None or t == 0:
        return 0
    if isinstance(t, (int, float)) and not isinstance(t, bool):
        if t < 0:
            raise ValueError("time must be >= 0")
        return int(round(t * 1000))
    m = _TIME.match(str(t))
    if not m:
        raise ValueError(f"cannot parse time {t!r}; use e.g. '30s', '2m', '1h', '1d'")
    v, u = float(m.group(1)), m.group(2) or "s"
    return int(round(v * {"s": 1000, "m": 60_000, "h": 3_600_000, "d": 86_400_000}[u]))


class Control:
    """Mirrors Fleet's Control plus the FleetArgs that matter for a run.

    steps=0 and time=0 together mean "run until Ctrl-C", as in Fleet; fleetpy refuses that
    unless forever=True, because it is almost always a mistake in a script.
    """

    def __init__(self, steps: int = 0, time=0, threads: int = 1, restart: int = 0, seed: int = 0,
                 max_nodes: int = 64, forever: bool = False):
        if not isinstance(steps, int) or steps < 0:
            raise ValueError("steps must be an int >= 0")
        if not isinstance(threads, int) or threads < 1:
            raise ValueError("threads must be an int >= 1")
        if not isinstance(seed, int) or seed < 0 or seed >= 2**64:
            raise ValueError("seed must be an int in [0, 2^64); 0 means unseeded")
        self.steps = steps
        self.time_ms = _parse_time_ms(time)
        self.threads = threads
        self.restart = int(restart)
        self.seed = seed
        self.max_nodes = int(max_nodes)
        if steps == 0 and self.time_ms == 0 and not forever:
            raise ValueError("Control: give steps= or time= (or forever=True to run until Ctrl-C)")

    def __repr__(self):
        return (f"Control(steps={self.steps}, time_ms={self.time_ms}, threads={self.threads}, "
                f"restart={self.restart}, seed={self.seed}, max_nodes={self.max_nodes})")


class MCMCChain:
    """A single chain; Fleet asserts it runs with threads=1."""
    algo = 0

    def __init__(self):
        self.chains, self.max_temp = 1, 1.0


class ChainPool:
    algo = 1

    def __init__(self, chains: int):
        if chains < 1:
            raise ValueError("chains must be >= 1")
        self.chains, self.max_temp = int(chains), 1.0


class ParallelTempering:
    algo = 2

    def __init__(self, chains: int, max_temp: float):
        if chains < 1 or not (max_temp >= 1.0):
            raise ValueError("ParallelTempering needs chains >= 1 and max_temp >= 1")
        self.chains, self.max_temp = int(chains), float(max_temp)
