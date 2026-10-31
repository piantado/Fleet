"""
Likelihoods. Each returns the body of ``compute_single_likelihood(const datum_t& di)``.

Inside a body you may use:
  * ``di.input``, ``di.output``, ``di.reliability``, ``di.count``  (Fleet's defaultdatum_t)
  * deterministic hypotheses: ``fp_call(x)`` -> ``std::pair<bool, output_t>``: (ok, value).
    ok is false when the program raised a VMSRuntimeError (or ran too long).
  * stochastic hypotheses: ``fp_dist(x)`` -> Fleet's ``DiscreteDistribution<output_t>``
    (unnormalized log masses; ``.Z()`` is their logsumexp, ``.values()`` the map).
  * any Fleet function (e.g. ``p_delete_append``, ``logplusexp``).

``nonpositive`` declares that every per-datum log-likelihood is <= 0. Fleet's likelihood
breakout (FleetArgs::LIKELIHOOD_BREAKOUT) is only valid under that condition, and the
assert that checks it is compiled out under -DNDEBUG. fleetpy therefore turns breakout off
for any likelihood that does not declare it.
"""

from __future__ import annotations

import math

from . import ftypes as T


class Likelihood:
    def __init__(self, body: str, nonpositive: bool, name: str, check_datum=None, params=None, needs=None):
        if needs not in (None, "value", "dist"):
            raise ValueError("needs must be None, 'value' (deterministic) or 'dist' (stochastic)")
        self.needs = needs  # what the body calls: fp_call ("value"), fp_dist ("dist"), or unknown
        self.body = body
        self.nonpositive = bool(nonpositive)
        self.name = name
        self.check_datum = check_datum  # python fn(datum, output_type) raising on invalid data
        self.params = params or {}

    def spec_dict(self):
        return {"name": self.name, "body": self.body, "nonpositive": self.nonpositive,
                "params": self.params, "needs": self.needs}


def _check_reliability(d, out_t):
    r = d.reliability
    if r is None or not (0.0 <= r <= 1.0) or math.isnan(r):
        raise ValueError(f"noisy_exact needs 0 <= reliability <= 1 on every datum; got {r!r}")


def noisy_exact(on_error: str = "mismatch") -> Likelihood:
    """RationalRules' likelihood: with probability r the output is right, otherwise a coin flip.

        log( (1-r)/2 + [out == y] * r )

    on_error:
      "mismatch" (default) -- a program that errors on this input counts as wrong.
      "default"            -- Fleet's behavior in RationalRules: an error returns output_t{}
                              (false for bool), which is compared to y like any output.
    Note the (1-r)/2 term assumes a *binary* output; for other output types it is still a
    valid (unnormalized over outputs) score but not a normalized likelihood.
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
    return Likelihood(body, True, "noisy_exact", _check_reliability, {"on_error": on_error}, needs="value")


def exact() -> Likelihood:
    """0 if the output matches exactly, -inf otherwise (and -inf on error)."""
    body = ("auto [fp_ok, fp_out] = fp_call(di.input);\n"
            "return (fp_ok and fp_out == di.output) ? 0.0 : -infinity;")
    return Likelihood(body, True, "exact", needs="value")


def gaussian(sd: float) -> Likelihood:
    """Normal density of the datum's output around the program's (float) output.

    Can be positive (sd < 1/sqrt(2*pi) ~ 0.399 at the mode), so breakout is disabled.
    """
    if not (sd > 0):
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
    return Likelihood(body, sd >= 1.0 / math.sqrt(2.0 * math.pi), "gaussian", check, {"sd": sd}, needs="value")


def cpp(body: str, nonpositive: bool = False) -> Likelihood:
    """A custom C++ body for compute_single_likelihood (tier 1)."""
    if not isinstance(body, str) or "return" not in body:
        raise ValueError("a cpp likelihood body must contain a return statement")
    needs = "dist" if "fp_dist" in body else ("value" if "fp_call" in body else None)
    return Likelihood(body, nonpositive, "cpp", needs=needs)


# ---- stochastic hypotheses ----------------------------------------------------------------

def stochastic_exact(normalize: bool = True) -> Likelihood:
    """log P(program outputs y | x).

    normalize=True divides by the distribution's total mass Z (the mass that completed without
    error and was not pruned), i.e. conditions on the program producing *some* output.
    normalize=False uses the raw mass (pruned/errored paths count as not producing y).
    A program with no completed path scores -inf either way.
    """
    z = "fp_d.Z()" if normalize else "0.0"
    body = (f"auto fp_d = fp_dist(di.input);\n"
            f"if(fp_d.size() == 0) return -infinity;\n"
            f"if(not fp_d.contains(di.output)) return -infinity;\n"
            f"return fp_d.get(di.output, -infinity) - {z};")
    return Likelihood(body, True, "stochastic_exact", params={"normalize": normalize}, needs="dist")


def string_edit(add_p: float, del_p: float, alphabet: str, normalize: bool = True) -> Likelihood:
    """Models/FormalLanguageTheory's string likelihood: the program's output string s is
    corrupted into the datum's output y by deleting from the end (each deletion with
    probability del_p) and then appending (each with probability add_p, characters uniform on
    the alphabet), marginalized over the program's output distribution:

        log sum_s P(s | x) * p_delete_append(s, y)

    with P normalized by Z when normalize=True (as in FormalLanguageTheory-Simple).
    Uses Fleet's p_delete_append (src/Strings.h).
    """
    for n, v in (("add_p", add_p), ("del_p", del_p)):
        if not (0.0 < v < 1.0):
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
    lk = Likelihood(body, True, "string_edit", check,
                    {"add_p": add_p, "del_p": del_p, "alphabet": alphabet, "normalize": normalize}, needs="dist")
    # p_delete_append takes its probabilities as `const float&` template arguments, so they
    # are emitted as namespace-scope constants in the generated code.
    lk.constants = {"fp_add_p": ("const float", repr(float(add_p)) + "f"),
                    "fp_del_p": ("const float", repr(float(del_p)) + "f")}
    return lk


def cpp_total(body: str) -> Likelihood:
    """A custom C++ body for the *whole-dataset* likelihood (overrides Bayesable's
    compute_likelihood(data, breakout)): it sees ``data`` (a span of datums) and returns the
    total log likelihood. Use this when the likelihood is not a sum over datums, as in
    Models/Number-Simple. Fleet's breakout is not applied.
    """
    if not isinstance(body, str) or "return" not in body:
        raise ValueError("a cpp_total likelihood body must contain a return statement")
    needs = "dist" if "fp_dist" in body else ("value" if "fp_call" in body else None)
    lk = Likelihood(body, False, "cpp_total", needs=needs)
    lk.total = True
    return lk
