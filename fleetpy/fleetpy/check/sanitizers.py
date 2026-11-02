"""
Running models under AddressSanitizer + UndefinedBehaviorSanitizer.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile

from .differential import CheckFailed

# Known upstream Fleet reports, suppressed narrowly (by check type and type name) so that a
# checked run still reports everything else. Each entry: (UBSan suppression, explanation).
KNOWN_FLEET_UBSAN = [
    ("vptr_check:Node",
     "src/BaseNode.h:252: BaseNode's constructors call fix_child_info(), which stores "
     "static_cast<Node*>(this) while Node is not yet constructed (formally UB; only the pointer "
     "is stored). Upstream Fleet issue, not fleetpy. UBSan matches vptr suppressions on the "
     "target type, so this silences dynamic-type checks of casts *to Node* only."),
]


def run_checked(script, *args, timeout=None, cxx=None, suppress_known_fleet=True):
    """Run a Python script in a child process with every fleetpy model built in 'checked'
    mode (ASan + UBSan). The ASan runtime must be loaded before Python starts, hence the
    separate process. Returns the CompletedProcess; raises CheckFailed if the child reports
    a sanitizer error or exits non-zero.

    Leak detection is off: CPython itself holds allocations at exit that LSan would report.
    """
    from ..build import _cxx
    cc = cxx or _cxx()

    def runtime(name):
        return subprocess.run([cc, f"-print-file-name={name}"], capture_output=True, text=True).stdout.strip()
    asan, ubsan = runtime("libasan.so"), runtime("libubsan.so")
    if not os.path.isabs(asan):
        raise RuntimeError(f"{cc} has no libasan.so")
    env = dict(os.environ)
    env["LD_PRELOAD"] = " ".join(x for x in (asan, ubsan, env.get("LD_PRELOAD", "")) if x)
    env["FLEETPY_MODE"] = "checked"
    pkg_parent = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    env["PYTHONPATH"] = os.pathsep.join(x for x in (pkg_parent, env.get("PYTHONPATH", "")) if x)
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1:" + env.get("ASAN_OPTIONS", "")
    ub = "print_stacktrace=1:halt_on_error=1:"
    if suppress_known_fleet and KNOWN_FLEET_UBSAN:
        with tempfile.NamedTemporaryFile("w", suffix=".supp", delete=False) as sup:
            sup.write("\n".join(x for x, _ in KNOWN_FLEET_UBSAN) + "\n")
        ub += f"suppressions={sup.name}:"
    env["UBSAN_OPTIONS"] = ub + env.get("UBSAN_OPTIONS", "")
    p = subprocess.run([sys.executable, str(script), *map(str, args)], env=env, capture_output=True,
                       text=True, timeout=timeout)
    out = p.stdout + p.stderr
    if p.returncode != 0 or "runtime error:" in out or "AddressSanitizer" in out:
        raise CheckFailed(f"checked run of {script} failed (exit {p.returncode}):\n{out[-6000:]}")
    return p
