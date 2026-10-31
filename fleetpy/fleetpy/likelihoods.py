"""
Likelihoods.

A ``Likelihood`` is C++ code plus the facts fleetpy needs to use it correctly. Its ``body``
is the body of either

  scope="datum": ``double compute_single_likelihood(const datum_t& di)`` (summed by Fleet), or
  scope="total": ``double compute_likelihood(const data_t& data, ...)`` (the whole dataset).

Inside a body:
  * ``di.input``, ``di.output``, ``di.reliability``, ``di.count`` (Fleet's defaultdatum_t)
  * deterministic hypotheses: ``fp_call(x)`` -> ``std::pair<bool, output_t>``: (ok, value),
    with ok false when the program raised VMSRuntimeError or ran too long
  * stochastic hypotheses: ``fp_dist(x)`` -> Fleet's ``DiscreteDistribution<output_t>``
    (unnormalized log masses; ``.Z()`` is their logsumexp), and ``fp_logprob(x, y)``
  * any Fleet function (``p_delete_append``, ``logplusexp``, ...)

``nonpositive`` declares every per-datum term <= 0. Fleet's likelihood breakout
(FleetArgs::LIKELIHOOD_BREAKOUT) is only valid under that condition, and the assert that
checks it is compiled out under -DNDEBUG, so fleetpy turns breakout off otherwise.
"""

from __future__ import annotations

import dataclasses
import math
import typing


@dataclasses.dataclass(frozen=True)
class Likelihood:
    body: str
    nonpositive: bool
    name: str
    needs: str = None             # what the body calls: "value" (fp_call), "dist" (fp_dist), or None
    scope: str = "datum"          # "datum" | "total"
    check_datum: typing.Callable = None   # fn(datum, output_type) raising on invalid data
    params: dict = dataclasses.field(default_factory=dict)
    constants: tuple = ()         # ((C++ type, name, value), ...) emitted at namespace scope
    transpiled: bool = False      # compiled from Python (then pyfn is its source function)
    pyfn: typing.Callable = None
    types: tuple = ()             # FTypes used inside the body (their definitions are emitted)

    def __post_init__(self):
        if self.needs not in (None, "value", "dist"):
            raise ValueError("needs must be None, 'value' (deterministic) or 'dist' (stochastic)")
        if self.scope not in ("datum", "total"):
            raise ValueError("scope must be 'datum' or 'total'")

    def spec(self):
        return {"name": self.name, "body": self.body, "nonpositive": self.nonpositive,
                "params": self.params, "needs": self.needs, "scope": self.scope,
                "constants": [list(c) for c in self.constants]}


def _needs(body):
    return "dist" if "fp_dist" in body else ("value" if "fp_call" in body else None)


def _check_reliability(d, out_t):
    r = d.reliability
    if r is None or math.isnan(r) or not (0.0 <= r <= 1.0):
        raise ValueError(f"noisy_exact needs 0 <= reliability <= 1 on every datum; got {r!r}")


# ---- deterministic ------------------------------------------------------------------------

def noisy_exact(on_error: str = "mismatch") -> Likelihood:
    """RationalRules' likelihood: with probability r the output is right, otherwise a coin flip.

        log( (1-r)/2 + [out == y] * r )

    on_error="mismatch" (default): a program that errors on this input counts as wrong.
    on_error="default": Fleet's behavior in RationalRules: an error returns output_t{}
    (false for bool), which is compared to y like any output.
    The (1-r)/2 term assumes a *binary* output; for other output types it is still a valid
    (unnormalized over outputs) score but not a normalized likelihood.
    """
    if on_error == "mismatch":
        body = ("auto [fp_ok, fp_out] = fp_call(di.input);\n"
                "return log((1.0-di.reliability)/2.0 + (fp_ok and fp_out == di.output)*di.reliability);")
    elif on_error == "default":
        body = ("output_t fp_out = this->call(di.input, output_t{});\n"
                "return log((1.0-di.reliability)/2.0 + (fp_out == di.output)*di.reliability);")
    else:
        raise ValueError("on_error must be 'mismatch' or 'default'")
    # (1-r)/2 + r*[eq] <= 1 for r in [0,1], so every term is <= 0.
    return Likelihood(body, True, "noisy_exact", "value", check_datum=_check_reliability,
                      params={"on_error": on_error})


def exact() -> Likelihood:
    """0 if the output matches exactly, -inf otherwise (and -inf on error)."""
    return Likelihood("auto [fp_ok, fp_out] = fp_call(di.input);\n"
                      "return (fp_ok and fp_out == di.output) ? 0.0 : -infinity;", True, "exact", "value")


def gaussian(sd: float) -> Likelihood:
    """Normal density of the datum's output around the program's (float) output.

    Positive near the mode when sd < 1/sqrt(2 pi) ~ 0.399, so breakout is then disabled.
    """
    if not sd > 0:
        raise ValueError("sd must be > 0")
    sd = float(sd)
    body = (f"auto [fp_ok, fp_out] = fp_call(di.input);\n"
            f"if(not fp_ok) return -infinity;\n"
            f"const double fp_sd = {sd!r};\n"
            f"const double fp_d = double(fp_out) - double(di.output);\n"
            f"return -0.5*log(2.0*M_PI*fp_sd*fp_sd) - fp_d*fp_d/(2.0*fp_sd*fp_sd);")

    def check(d, out_t):
        if out_t.key != "f64":
            raise TypeError("gaussian likelihood needs a float output type")
    return Likelihood(body, sd >= 1.0 / math.sqrt(2.0 * math.pi), "gaussian", "value",
                      check_datum=check, params={"sd": sd})


def cpp(body: str, nonpositive: bool = False) -> Likelihood:
    """A custom C++ body for compute_single_likelihood."""
    if not isinstance(body, str) or "return" not in body:
        raise ValueError("a cpp likelihood body must contain a return statement")
    return Likelihood(body, nonpositive, "cpp", _needs(body))


def cpp_total(body: str) -> Likelihood:
    """A custom C++ body for the *whole-dataset* likelihood: it sees ``data`` (a span of
    datums) and returns the total log likelihood, for likelihoods that are not a sum over
    datums (as in Models/Number-Simple). Fleet's breakout is not applied."""
    if not isinstance(body, str) or "return" not in body:
        raise ValueError("a cpp_total likelihood body must contain a return statement")
    return Likelihood(body, False, "cpp_total", _needs(body), scope="total")


# ---- stochastic ----------------------------------------------------------------------------

def stochastic_exact(normalize: bool = True) -> Likelihood:
    """log P(program outputs y | x).

    normalize=True divides by the distribution's total mass Z (the mass that completed without
    error and was not pruned), i.e. conditions on the program producing *some* output;
    normalize=False uses the raw mass. A program with no completed path scores -inf.
    """
    z = "fp_d.Z()" if normalize else "0.0"
    body = (f"auto fp_d = fp_dist(di.input);\n"
            f"if(fp_d.size() == 0) return -infinity;\n"
            f"if(not fp_d.contains(di.output)) return -infinity;\n"
            f"return fp_d.get(di.output, -infinity) - {z};")
    return Likelihood(body, True, "stochastic_exact", "dist", params={"normalize": normalize})


def string_edit(add_p: float, del_p: float, alphabet: str, normalize: bool = True) -> Likelihood:
    """Models/FormalLanguageTheory's string likelihood: the program's output s is corrupted
    into the datum's output y by deleting from the end (each deletion with probability del_p)
    and then appending (each with probability add_p, characters uniform on the alphabet),
    marginalized over the program's output distribution:

        log sum_s P(s | x) * p_delete_append(s, y)

    with P normalized by Z when normalize=True (as in FormalLanguageTheory-Simple).
    """
    for n, v in (("add_p", add_p), ("del_p", del_p)):
        if not 0.0 < v < 1.0:
            raise ValueError(f"{n} must be in (0, 1)")
    if not alphabet or len(set(alphabet)) != len(alphabet):
        raise ValueError("alphabet must be a non-empty string of distinct characters")

    def check(d, out_t):
        if out_t.key != "str":
            raise TypeError("string_edit needs a str output type")
        if any(c not in alphabet for c in d.output):
            raise ValueError(f"datum output {d.output!r} has characters outside the alphabet {alphabet!r}")
    z = "fp_d.Z()" if normalize else "0.0"
    body = (f"auto fp_d = fp_dist(di.input);\n"
            f"const double fp_z = {z};\n"
            f"const float fp_logA = log({len(alphabet)});\n"
            f"double fp_lp = -infinity;\n"
            f"for(auto& [fp_s, fp_slp] : fp_d.values())\n"
            f"\tfp_lp = logplusexp(fp_lp, (fp_slp - fp_z) + p_delete_append<fp_add_p, fp_del_p>(fp_s, di.output, fp_logA));\n"
            f"return fp_lp;")
    # p_delete_append takes its probabilities as `const float&` template arguments, which
    # must name objects at namespace scope.
    constants = (("const float", "fp_add_p", repr(float(add_p)) + "f"),
                 ("const float", "fp_del_p", repr(float(del_p)) + "f"))
    return Likelihood(body, True, "string_edit", "dist", check_datum=check, constants=constants,
                      params={"add_p": add_p, "del_p": del_p, "alphabet": alphabet, "normalize": normalize})
