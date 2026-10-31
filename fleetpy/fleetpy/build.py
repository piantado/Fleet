"""
Compiling generated code and caching the result.

Flags come from the Fleet tree's own Fleet.mk (FLEET_FLAGS, FLEET_INCLUDE) so that fleetpy
compiles exactly what a hand-written model would. The cache key covers everything that can
change the binary: generated source, compiler identity, flags, the Fleet commit plus any
uncommitted changes under src/, nanobind's version, fleetpy's own C++ headers and version.
"""

from __future__ import annotations

import hashlib
import importlib.machinery
import importlib.util
import os
import re
import shlex
import socket
import subprocess
import sys
import sysconfig
import threading
import warnings
from pathlib import Path

from .codegen import CPP_DIR   # fleetpy's own C++ headers

FLEETPY_VERSION = "0.2.0"

_config = {
    "fleet_root": None,
    "nanobind_dir": None,
    "cxx": None,
    "cache_dir": None,
    "extra_includes": [],   # e.g. eigen, or a model's own headers
    "verbose": False,
}

_build_lock = threading.Lock()


def config(**kw):
    """Set build configuration: fleet_root, nanobind_dir, cxx, cache_dir, extra_includes, verbose."""
    for k, v in kw.items():
        if k not in _config:
            raise KeyError(f"unknown config key {k!r}; known: {sorted(_config)}")
        _config[k] = v


def fleet_root() -> Path:
    r = _config["fleet_root"] or os.environ.get("FLEET_ROOT")
    if r is None:
        # default: the Fleet tree this package lives in (Fleet/fleetpy/fleetpy)
        here = Path(__file__).resolve().parent.parent.parent
        if (here / "Fleet.mk").exists() and (here / "src" / "Fleet.h").exists():
            r = here
    if r is None:
        raise RuntimeError("cannot find Fleet: set FLEET_ROOT or fleetpy.config(fleet_root=...)")
    r = Path(r).resolve()
    if not (r / "Fleet.mk").exists():
        raise RuntimeError(f"{r} does not look like a Fleet tree (no Fleet.mk)")
    return r


def nanobind_dir() -> Path:
    d = _config["nanobind_dir"] or os.environ.get("FLEETPY_NANOBIND")
    if d is None:
        try:
            import nanobind  # type: ignore
            d = Path(nanobind.__file__).parent
        except ImportError:
            pass
    if d is None:
        raise RuntimeError("cannot find nanobind: pip install nanobind, or set FLEETPY_NANOBIND to "
                           "a nanobind checkout (with ext/robin_map)")
    d = Path(d).resolve()
    inc = d / "include" / "nanobind" / "nanobind.h"
    if not inc.exists():
        raise RuntimeError(f"{d} is not a nanobind tree (missing include/nanobind/nanobind.h)")
    return d


def _cxx() -> str:
    return _config["cxx"] or os.environ.get("CXX") or "g++"


def _run(cmd, cwd=None) -> str:
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        raise BuildError(cmd, p.stdout)
    return p.stdout


class BuildError(RuntimeError):
    def __init__(self, cmd, output):
        self.cmd, self.output = cmd, output
        # keep the message readable: first errors, not thousands of template lines
        errs = [ln for ln in output.splitlines() if "error" in ln]
        head = "\n".join(errs[:15]) if errs else output[-4000:]
        super().__init__(f"compilation failed:\n  {shlex.join(cmd)[:400]}...\n{head}\n"
                         f"(full log on the exception's .output)")


def _makefile_var(text: str, name: str, root: Path) -> list[str]:
    m = re.search(rf"^{name}\s*=(.*)$", text, re.M)
    if not m:
        raise RuntimeError(f"Fleet.mk has no {name}")
    v = m.group(1).replace("$(FLEET_ROOT)", str(root))
    if "$(" in v:
        raise RuntimeError(f"Fleet.mk's {name} uses a make variable fleetpy cannot expand: {v}")
    return shlex.split(v)


_std_cache: dict = {}


def _supported_std(cxx: str, wanted: str) -> tuple[str, str | None]:
    """Return a -std flag the compiler accepts, preferring Fleet's own; and a warning if downgraded."""
    if (cxx, wanted) in _std_cache:
        return _std_cache[(cxx, wanted)]
    order = [wanted]
    if wanted == "-std=c++26":
        order += ["-std=c++2c", "-std=c++23"]
    for flag in order:
        p = subprocess.run([cxx, flag, "-x", "c++", "-fsyntax-only", "-"], input="int main(){}",
                           text=True, capture_output=True)
        if p.returncode == 0:
            warn = None if flag == wanted else (
                f"{cxx} does not accept {wanted}; fleetpy is compiling with {flag} instead. "
                f"Fleet itself targets {wanted} (g++ >= 14).")
            _std_cache[(cxx, wanted)] = (flag, warn)
            return flag, warn
    raise RuntimeError(f"{cxx} accepts none of {order}")


MODES = {
    "release": ["-O2", "-DNDEBUG"],
    "debug":   ["-O0", "-g"],
    "checked": ["-O1", "-g", "-D_GLIBCXX_ASSERTIONS", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer"],
}


def flags(mode: str) -> tuple[list[str], list[str], list[str]]:
    """(compile flags, include flags, warnings) for this Fleet tree and mode."""
    if mode not in MODES:
        raise ValueError(f"mode must be one of {sorted(MODES)}")
    root = fleet_root()
    mk = (root / "Fleet.mk").read_text()
    ff = _makefile_var(mk, "FLEET_FLAGS", root)
    inc = _makefile_var(mk, "FLEET_INCLUDE", root)
    notes = []
    std = [f for f in ff if f.startswith("-std=")]
    if len(std) != 1:
        raise RuntimeError(f"expected exactly one -std= in FLEET_FLAGS, got {std}")
    newstd, warn = _supported_std(_cxx(), std[0])
    if warn:
        notes.append(warn)
    ff = [newstd if f == std[0] else f for f in ff]
    # -fvar-tracking-assignments is only meaningful with -g; harmless otherwise. Keep Fleet's flags.
    nb = nanobind_dir()
    inc = inc + [f"-I{CPP_DIR}", f"-I{nb / 'include'}", f"-I{nb / 'ext' / 'robin_map' / 'include'}",
                 f"-I{sysconfig.get_paths()['include']}"]
    for e in _config["extra_includes"]:
        inc.append(f"-I{e}")
    return ff + MODES[mode] + ["-fPIC", "-fvisibility=hidden"], inc, notes


def _git_state(root: Path) -> str:
    """Fleet commit + hash of uncommitted changes under src/ (so stale modules are never reused)."""
    try:
        head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
        diff = subprocess.run(["git", "-C", str(root), "diff", "HEAD", "--", "src"],
                              capture_output=True, text=True, check=True).stdout
        untracked = subprocess.run(["git", "-C", str(root), "ls-files", "--others",
                                    "--exclude-standard", "src"],
                                   capture_output=True, text=True, check=True).stdout
        h = hashlib.sha256((diff + untracked).encode()).hexdigest()[:16]
        for f in sorted(untracked.split()):
            h = hashlib.sha256((h + (root / f).read_text(errors="replace")).encode()).hexdigest()[:16]
        return f"{head}+{h}"
    except (subprocess.CalledProcessError, FileNotFoundError):
        # not a git checkout: hash every header under src/
        hs = hashlib.sha256()
        for p in sorted((root / "src").rglob("*")):
            if p.is_file():
                hs.update(str(p.relative_to(root)).encode())
                hs.update(p.read_bytes())
        return "nogit+" + hs.hexdigest()[:16]


def _compiler_id(cxx: str) -> str:
    out = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout
    path = subprocess.run(["which", cxx], capture_output=True, text=True).stdout.strip()
    return path + "\n" + out


def cache_dir() -> Path:
    d = _config["cache_dir"] or os.environ.get("FLEETPY_CACHE") or (
        Path.home() / ".cache" / "fleetpy" / socket.gethostname())
    d = Path(d)
    d.mkdir(parents=True, exist_ok=True)
    return d


def _nanobind_version(nb: Path) -> str:
    h = (nb / "include" / "nanobind" / "nanobind.h").read_text()
    v = re.findall(r"#define NB_VERSION_(MAJOR|MINOR|PATCH) (\d+)", h)
    return ".".join(x for _, x in v) or hashlib.sha256(h.encode()).hexdigest()[:12]


def _nanobind_object(cf, inc, key: str) -> Path:
    """libnanobind compiled once per flag set (nb_combined.cpp is nanobind's single-TU build)."""
    nb = nanobind_dir()
    out = cache_dir() / f"libnanobind-{key}.o"
    if out.exists():
        return out
    src = nb / "src" / "nb_combined.cpp"
    # nanobind's own build uses -fno-strict-aliasing for its library
    flt = [f for f in cf if not f.startswith("-W")] + ["-fno-strict-aliasing", "-w"]
    tmp = out.with_suffix(f".tmp{os.getpid()}.o")
    _run([_cxx(), "-c", str(src), "-o", str(tmp)] + flt + inc)
    os.replace(tmp, out)
    return out


def build_module(source: str, spec_json: str, mode: str = "release"):
    """Compile ``source`` (whose NB_MODULE name is a placeholder) and import it.

    Returns (module, info dict). Cached: an identical build is a dictionary lookup.
    """
    cf, inc, notes = flags(mode)
    root = fleet_root()
    nb = nanobind_dir()
    keyparts = {
        "source": source, "flags": cf, "includes": [i for i in inc],
        "compiler": _compiler_id(_cxx()), "fleet": _git_state(root),
        "nanobind": _nanobind_version(nb), "fleetpy": FLEETPY_VERSION,
        "python": sysconfig.get_config_var("EXT_SUFFIX"),
        "headers": {p.name: p.read_text() for p in sorted(CPP_DIR.glob("*.h"))},
    }
    key = hashlib.sha256(repr(sorted(keyparts.items())).encode()).hexdigest()
    modname = f"fp_{key[:20]}"
    src = source.replace("FLEETPY_MODULE_NAME", modname).replace("FLEETPY_NAMESPACE", modname + "_ns")
    d = cache_dir() / modname
    so = d / (modname + sysconfig.get_config_var("EXT_SUFFIX"))
    info = {"key": key, "module": modname, "dir": str(d), "mode": mode, "notes": notes,
            "fleet": keyparts["fleet"], "flags": cf, "cached": True}
    for n in notes:
        warnings.warn(n, stacklevel=3)
    with _build_lock:
        if not so.exists():
            info["cached"] = False
            d.mkdir(parents=True, exist_ok=True)
            (d / "model.cpp").write_text(src)
            (d / "spec.json").write_text(spec_json)
            nbkey = hashlib.sha256(repr((cf, inc, _nanobind_version(nb), _compiler_id(_cxx()))).encode()).hexdigest()[:16]
            nbo = _nanobind_object(cf, inc, nbkey)
            tmp = d / (so.name + f".tmp{os.getpid()}")
            cmd = [_cxx(), str(d / "model.cpp"), str(nbo), "-shared", "-o", str(tmp)] + cf + inc + [
                "-lm", "-pthread"]
            if mode == "checked":
                cmd.append("-fsanitize=address,undefined")
            out = _run(cmd)
            (d / "build.log").write_text(shlex.join(cmd) + "\n\n" + out)
            os.replace(tmp, so)  # atomic: a crashed build never leaves a half-written .so
    if mode == "checked" and "libasan" not in os.environ.get("LD_PRELOAD", ""):
        raise RuntimeError(
            "a 'checked' (ASan/UBSan) module must be loaded in a process started with the ASan "
            "runtime preloaded; use fleetpy.check.run_checked(...) which starts one for you. "
            f"(module built at {so})")
    return _import(modname, so), info


_loaded: dict = {}


def _import(modname: str, so: Path):
    if modname in _loaded:
        return _loaded[modname]
    loader = importlib.machinery.ExtensionFileLoader(modname, str(so))
    spec = importlib.util.spec_from_file_location(modname, str(so), loader=loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    sys.modules[modname] = mod
    _loaded[modname] = mod
    return mod
