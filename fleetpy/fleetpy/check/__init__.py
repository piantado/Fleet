"""
Correctness checks. Each returns a report dict and raises CheckFailed when it fails.

  differential(model)          Python compiled to C++ vs its source (run on every build)
  roundtrip(model, n)          text round trips on prior samples
  prior(model, n)              Fleet's prior vs an independent Python computation
  posterior(model, data, ...)  MCMC visit frequencies vs the exact posterior on a finite space
  run_checked(script)          a script with every model built under ASan + UBSan
"""

from .differential import CheckFailed, differential, same
from .posterior import chi2_sf, enumerate_programs, exact_posterior, posterior
from .reference import References
from .sanitizers import KNOWN_FLEET_UBSAN, run_checked
from .structure import prior, python_log_prior, roundtrip

__all__ = ["CheckFailed", "KNOWN_FLEET_UBSAN", "References", "chi2_sf", "differential", "enumerate_programs",
           "exact_posterior", "posterior", "prior", "python_log_prior", "roundtrip", "run_checked", "same"]
