"""
fleetpy test suite.  Run from Fleet/fleetpy:   python3 -m pytest -q tests

Environment: FLEETPY_NANOBIND must point at nanobind if it is not pip-installed.
"""
import math
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "examples"))

import fleetpy as fl                      # noqa: E402
from fleetpy import check as C            # noqa: E402
from fleetpy.transpile import TranspileError  # noqa: E402

import rational_rules as RR               # noqa: E402
import models_tier2 as M                  # noqa: E402

pytestmark = pytest.mark.filterwarnings("ignore::UserWarning")


@pytest.fixture(scope="session")
def rr():
    return fl.build(RR.MyHypothesis)


@pytest.fixture(scope="session")
def tier2():
    return fl.build(M.H)


@pytest.fixture(scope="session")
def tier2f():
    return fl.build(M.H2)


# --------------------------------------------------------------------------------------
# 1. equivalence with native Fleet
# --------------------------------------------------------------------------------------

def _native_driver():
    from fleetpy.build import flags
    exe = HERE / "native" / "rr_driver"
    src = HERE / "native" / "rr_driver.cpp"
    if not exe.exists() or exe.stat().st_mtime < src.stat().st_mtime:
        cf, inc, _ = flags("release")
        cf = [f for f in cf if f not in ("-fPIC", "-fvisibility=hidden")]
        inc = [i for i in inc if "nanobind" not in i and "python" not in i]
        subprocess.run(["g++", str(src), "-o", str(exe)] + cf + inc + ["-lm", "-pthread"], check=True)
    return exe


def _fnv(strings):
    h = 1469598103934665603
    for s in strings:
        for c in (s + "\n").encode():
            h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


@pytest.mark.parametrize("seed", [17, 4242])
def test_equivalence_with_native_rational_rules(rr, seed):
    """Same seed, one chain: fleetpy and Fleet's own Models/RationalRules must produce the same
    top hypotheses with bit-identical posteriors, and the same full sample trajectory."""
    steps = 100_000
    out = subprocess.run([str(_native_driver()), f"--seed={seed}", f"--steps={steps}", "--top=10"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    native_top = [ln.split("\t") for ln in out if not ln.startswith("#")]
    native_traj = [ln.split()[-1] for ln in out if ln.startswith("# trajectory")][0]

    res = rr.run(RR.DATA, fl.MCMCChain(), control=fl.Control(steps=steps, seed=seed), top=10)
    assert res.stats.samples == steps and res.stats.reproducible
    mine = [(h.posterior, h.prior, h.serialize()) for h in res.top]
    theirs = [(float.fromhex(a), float.fromhex(b), c) for a, b, c, _ in native_top]
    assert mine == theirs

    names, seq = rr.module.trace(rr._convert_data(RR.DATA), steps, 0, seed, 64, True, True)
    assert _fnv(names[i] for i in seq) == native_traj


def test_repeated_runs_identical(rr):
    c = fl.Control(steps=50_000, seed=99)
    a = rr.run(RR.DATA, fl.MCMCChain(), control=c, top=20)
    b = rr.run(RR.DATA, fl.MCMCChain(), control=c, top=20)
    assert [(h.serialize(), h.posterior) for h in a.top] == [(h.serialize(), h.posterior) for h in b.top]


# --------------------------------------------------------------------------------------
# 2. structural checks
# --------------------------------------------------------------------------------------

def test_roundtrip_and_prior(rr, tier2, tier2f):
    for m in (rr, tier2, tier2f):
        assert C.roundtrip(m, 300)["failures"] == 0
        assert C.prior(m, 300)["max_rel_error"] < 1e-12


def test_parse(rr):
    h = rr.parse("λx.and(blue(x),not(circle(x)))")
    assert h.string() == "λx.and(blue(x),not(circle(x)))"
    assert h.call(RR.Obj(0, 0, 0)) is True and h.call(RR.Obj(2, 0, 0)) is False
    for bad in ("and(blue(x))", "blue(x", "nosuch(x)", "x", "blue(blue(x))"):
        with pytest.raises((ValueError, TypeError)):
            rr.parse(bad)


def test_exact_posterior(rr):
    rep = C.posterior(rr, RR.DATA, max_nodes=5, steps=400_000, seed=7)
    assert rep["p_value"] > 1e-4 and rep["total_variation"] < 0.03


def test_exact_posterior_has_power(rr):
    """The test must reject a slightly wrong target (reliability 0.70 instead of 0.75)."""
    wrong = [fl.Datum(d.input, d.output, 0.70) for d in RR.DATA]
    ex = C.exact_posterior(rr, wrong, max_nodes=5)
    with pytest.raises(C.CheckFailed):
        C.posterior(rr, RR.DATA, max_nodes=5, steps=400_000, seed=7, exact=ex)


# --------------------------------------------------------------------------------------
# 3. tier 2
# --------------------------------------------------------------------------------------

def test_differential_reports(tier2, tier2f):
    m3 = fl.build(M.H3)
    assert m3.parse("push(rot(x),'q')").call("abc") == "bcaqq"
    for m in (tier2, tier2f, m3):
        rep = m.info["differential"]
        assert rep and all(v["mismatches"] == 0 for v in rep.values())
    assert tier2f.info["differential"]["likelihood"]["cases"] > 0


def test_likelihood_reference_is_int64():
    """The reference for a Python likelihood applies int64 semantics, like the compiled code
    (unbounded Python ints would disagree whenever y * 2^62 overflows)."""
    m = fl.build(M.H4)
    rep = m.info["differential"]["likelihood"]
    assert rep["cases"] > 0 and rep["mismatches"] == 0
    h = m.parse("x")
    assert h.compute_posterior([fl.Datum(0, 0)])[1] == 0.0
    assert h.compute_posterior([fl.Datum(2, 0)])[1] == -math.inf      # 2 * 2^62 overflows


def test_build_leaves_the_grammar_alone():
    """A Lexicon's key terminals go into the built model's grammar, not the user's."""
    import lexicon_arith as LA
    n = len(LA.g.rules)
    m = fl.build(LA.Lex)
    assert len(LA.g.rules) == n and len(m.grammar.rules) == n + 2
    assert [r.fmt for r in m.grammar.rules[n:]] == ["0", "1"]


def test_python_semantics_spot_checks(tier2):
    def h(prog, x):
        return tier2.parse(prog).call(x, default="undef")
    assert h("fdiv(x,one)", -7) == -7
    assert h("fdiv(neg(x),fdiv(fdiv(x,x),one))", 7) == -7
    assert h("pmod(neg(x),sq(one))", 7) == 0
    assert tier2.module.fp_prim_fdiv(-7, 2) == (True, -4)          # floor, not truncation
    assert tier2.module.fp_prim_pmod(-7, 2) == (True, 1)           # sign of divisor
    assert tier2.module.fp_prim_pmod(7, -2) == (True, -1)
    assert tier2.module.fp_prim_fdiv(1, 0)[0] is False             # ZeroDivisionError
    assert tier2.module.fp_prim_to8(128)[0] is False               # narrowing
    assert tier2.module.fp_prim_addc(2**63 - 1, 1)[0] is False     # overflow
    assert tier2.module.fp_prim_tr(-7) == (True, -2)               # int() truncates
    assert tier2.module.fp_prim_strstuff(0) == (True, 4 * 10 + 1 + 0)  # code points
    assert tier2.module.fp_prim_triangle(10**9)[0] is False        # iteration cap
    assert tier2.module.fp_prim_fallthrough(-1)[0] is False        # fell off the end
    assert tier2.module.fp_prim_pick(3, 3)[0] is False             # raise Undefined


def _transpile_error(src_fn):
    g = fl.Grammar(input=int, output=int)
    g.primitive(src_fn)
    g.add_builtin(fl.builtins.X)

    class Hx(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TranspileError):
        fl.build(Hx)


def bad_truthy(a: int) -> int:
    if a:
        return 1
    return 0


def bad_mixed_cmp(a: int) -> int:
    return 1 if a < 0.5 else 0


def bad_float_floordiv(a: int) -> int:
    return math.floor(float(a) // 2.0)


def bad_unbound(a: int) -> int:
    if a > 0:
        r = 1
    return r


def bad_while(a: int) -> int:
    while a > 0:
        a = a - 1
    return a


def bad_param_assign(a: int) -> int:
    a = 3
    return a


def bad_retype(a: int) -> int:
    r = 1
    r = 1.5
    return a


def bad_bool_arith(a: int) -> int:
    return (a > 0) + 1


def bad_hypot(a: int) -> int:
    return math.floor(math.hypot(a, a))


def bad_tier1_call(a: int) -> int:
    return len([a]) + undefined_function(a)  # noqa: F821


def bad_pow(a: int) -> int:
    return a ** a


def bad_slice_step(a: int) -> int:
    return len("abc"[::2])


MIXED = [1, 2.5]


def bad_mixed_constant(a: int) -> int:
    return math.floor(MIXED[0])


def bad_unknown_global(a: int) -> int:
    return len(math.__name__)


@pytest.mark.parametrize("fn", [bad_slice_step, bad_mixed_constant, bad_unknown_global, bad_truthy, bad_mixed_cmp, bad_float_floordiv, bad_unbound, bad_while,
                                bad_param_assign, bad_retype, bad_bool_arith, bad_hypot, bad_tier1_call,
                                bad_pow])
def test_transpiler_rejects(fn):
    _transpile_error(fn)


# --------------------------------------------------------------------------------------
# 4. grammar and data validation
# --------------------------------------------------------------------------------------

def test_default_and_explicit_formats():
    assert fl.default_format("f", 0) == "f" and fl.default_format("f", 2) == "f(%s,%s)"
    g = fl.Grammar(input=int, output=int)

    @g.primitive(fmt="(%s + %s)")
    def plus(a: int, b: int) -> int:
        return a + b
    assert g.rules[0].fmt == "(%s + %s)"
    with pytest.raises(ValueError):
        @g.primitive(fmt="(%s + %s + %s)")
        def plus3(a: int, b: int) -> int:
            return a + b


def test_duplicate_formats_rejected():
    g = fl.Grammar(input=int, output=int)

    @g.primitive(fmt="f(%s)")
    def a1(x: int) -> int:
        return x

    @g.primitive(fmt="f(%s)")
    def a2(x: int) -> int:
        return x
    g.add_builtin(fl.builtins.X)
    with pytest.raises(ValueError, match="share the format"):
        g.check()


def test_unproducible_type_rejected():
    g = fl.Grammar(input=int, output=int)

    @g.primitive
    def need_str(s: str) -> int:
        return len(s)
    g.add_builtin(fl.builtins.X)
    with pytest.raises(ValueError, match="no rule produces"):
        g.check()


def test_infinite_type_rejected():
    g = fl.Grammar(input=int, output=int)

    @g.primitive
    def loop(s: str) -> str:
        return s

    @g.primitive
    def use(s: str) -> int:
        return len(s)
    g.add_builtin(fl.builtins.X)
    with pytest.raises(ValueError, match="finite"):
        g.check()


def test_data_validation(rr):
    with pytest.raises(ValueError, match="reliability"):
        rr.run([fl.Datum(RR.Obj(0, 0, 0), True)], control=fl.Control(steps=10))
    with pytest.raises(OverflowError):
        rr.run([fl.Datum(RR.Obj(300, 0, 0), True, 0.9)], control=fl.Control(steps=10))
    with pytest.raises(TypeError):
        rr.run([fl.Datum(RR.Obj(0, 0, 0), 1, 0.9)], control=fl.Control(steps=10))
    with pytest.raises(ValueError):
        fl.Control()  # neither steps nor time


def test_breakout_flags():
    assert fl.likelihoods.noisy_exact().nonpositive
    assert not fl.likelihoods.gaussian(0.1).nonpositive          # density can exceed 1
    assert fl.likelihoods.gaussian(1.0).nonpositive
    assert not fl.likelihoods.cpp("return 1.0;").nonpositive


# --------------------------------------------------------------------------------------
# 5. tier 3, eject, sanitizers
# --------------------------------------------------------------------------------------

def test_python_callback_primitive():
    calls = []
    g = fl.Grammar(input=int, output=int)

    @g.primitive(backend="python")
    def pyinc(a: int) -> int:
        calls.append(a)
        if a > 100:
            raise fl.Undefined
        return a + 1

    @g.primitive
    def dbl(a: int) -> int:
        return a * 2
    g.add_builtin(fl.builtins.X)

    class H3(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    m = fl.build(H3)
    assert m.parse("pyinc(dbl(x))").call(5) == 11 and calls[-1] == 10
    with pytest.raises(fl.Undefined):
        m.parse("pyinc(x)").call(500)
    with pytest.raises(ValueError, match="threads=1"):
        m.run([fl.Datum(1, 2)], fl.ChainPool(2), control=fl.Control(steps=10, threads=2))
    r = m.run([fl.Datum(1, 4), fl.Datum(3, 8)], control=fl.Control(steps=20000, seed=1))
    assert r.top[0].likelihood == 0.0


def test_eject_builds_and_runs(tmp_path, rr):
    d = rr.eject(tmp_path / "RR", RR.DATA, fl.MCMCChain())
    cmd = ["make", "-s", "-C", str(d), "all"]
    from fleetpy.build import flags
    _, _, notes = flags("release")
    if notes:  # this compiler lacks Fleet's -std; build the ejected model the way fleetpy does
        import re
        from fleetpy.build import fleet_root
        ff = re.search(r"^FLEET_FLAGS\s*=(.*)$", (fleet_root() / "Fleet.mk").read_text(), re.M).group(1)
        std = [f for f in flags("release")[0] if f.startswith("-std=")][0]
        cmd.append("FLEET_FLAGS=" + re.sub(r"-std=\S+", std, ff))
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr
    out = subprocess.run([str(d / "main"), "--steps=20000", "--seed=5", "--top=3", "--header=0"],
                         capture_output=True, text=True, check=True).stdout
    assert "triangle(x)" in out


@pytest.mark.slow
def test_checked_build_is_clean():
    p = C.run_checked(HERE / "checked_smoke.py", timeout=1800)
    assert "checked-ok" in p.stdout


def test_two_models_same_names_different_layouts():
    """Each compiled model has its own C++ namespace, so same-named types cannot be confused."""
    import types as _t
    mods = []
    for fields in ({"a": int}, {"a": float, "b": str}):
        ns = _t.SimpleNamespace()
        Obj = type("Obj", (), {"__annotations__": fields})
        Obj = fl.struct(Obj)
        g = fl.Grammar(input=Obj, output=bool)
        if "b" in fields:
            g.primitive(cpp="x.a > 0.5 and x.b.size() > 1")(_sig(Obj, "big"))
        else:
            g.primitive(cpp="x.a > 1")(_sig(Obj, "big"))
        g.add_builtin(fl.builtins.X)
        H = type("MyHypothesis", (fl.DeterministicHypothesis,),
                 {"likelihood": fl.likelihoods.exact()}, grammar=g)
        mods.append((fl.build(H), Obj))
    (m1, O1), (m2, O2) = mods
    assert m1.parse("big(x)").call(O1(2)) is True
    assert m2.parse("big(x)").call(O2(0.75, "xy")) is True
    assert m2.parse("big(x)").call(O2(0.75, "x")) is False
    assert m1.parse("big(x)").call(O1(0)) is False


def _sig(T_, name):
    def f(x): ...
    f.__name__ = name
    f.__annotations__ = {"x": T_, "return": bool}
    return f
