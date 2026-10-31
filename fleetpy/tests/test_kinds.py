"""
Tests for stochastic hypotheses, recursion (Recurse/SafeRecurse/MemRecurse), and Lexicons.
"""
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "examples"))

import fleetpy as fl                       # noqa: E402
from fleetpy import builtins as B          # noqa: E402
from fleetpy import check as C             # noqa: E402

import models_kinds as M                   # noqa: E402
import formal_language as FLT              # noqa: E402
import lexicon_arith as LA                 # noqa: E402
import number as NUM                        # noqa: E402

pytestmark = pytest.mark.filterwarnings("ignore::UserWarning")

DATA_BOOL = [fl.Datum(True, True), fl.Datum(True, True), fl.Datum(True, False), fl.Datum(False, True)]


@pytest.fixture(scope="session")
def rec():
    return fl.build(M.Rec)


@pytest.fixture(scope="session")
def coin():
    return fl.build(M.Coin)


@pytest.fixture(scope="session")
def flt():
    return fl.build(FLT.MyHypothesis)


@pytest.fixture(scope="session")
def lexa():
    return fl.build(LA.Lex)


# --------------------------------------------------------------------------------------
# native equivalence: Fleet's own FormalLanguageTheory-Simple, and a hand-written Lexicon
# --------------------------------------------------------------------------------------

def _driver(name, extra_includes=()):
    from fleetpy.build import flags
    exe = HERE / "native" / name
    src = HERE / "native" / (name + ".cpp")
    if not exe.exists() or exe.stat().st_mtime < src.stat().st_mtime:
        cf, inc, _ = flags("release")
        cf = [f for f in cf if f not in ("-fPIC", "-fvisibility=hidden")]
        inc = [f"-I{HERE / 'native' / d}" for d in extra_includes] + \
              [i for i in inc if "nanobind" not in i and "python" not in i]
        subprocess.run(["g++", str(src), "-o", str(exe)] + cf + inc + ["-lm", "-pthread"], check=True)
    return exe


def _fnv(strings):
    h = 1469598103934665603
    for s in strings:
        for c in (s + "\n").encode():
            h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def _native(exe, seed, steps, top):
    out = subprocess.run([str(exe), f"--seed={seed}", f"--steps={steps}", f"--top={top}"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    rows = [ln.split("\t") for ln in out if not ln.startswith("#")]
    traj = [ln.split()[-1] for ln in out if ln.startswith("# trajectory")][0]
    return rows, traj


@pytest.mark.parametrize("seed", [17, 99])
def test_equivalence_formal_language_theory(flt, seed):
    """Stochastic + flip + Recurse + MemRecurse + terminals + custom proposal + string_edit
    likelihood + VMLimits: bit-identical to Fleet's Models/FormalLanguageTheory-Simple."""
    steps = 30_000
    rows, traj = _native(_driver("flt_driver", ["stub"]), seed, steps, 5)
    res = flt.run(FLT.DATA, fl.MCMCChain(), control=fl.Control(steps=steps, seed=seed), top=5)
    assert [(h.posterior, h.prior, h.serialize()) for h in res.top] == \
        [(float.fromhex(a), float.fromhex(b), c) for a, b, c, _ in rows]
    names, seq = flt.module.trace(flt._convert_data(FLT.DATA), steps, 0, seed, 64, True, True)
    assert _fnv(names[i] for i in seq) == traj
    assert any("F(%s)" in n for n in names) and any("Fm(%s)" in n for n in names)


@pytest.mark.parametrize("seed", [17, 5])
def test_equivalence_lexicon(lexa, seed):
    """Deterministic Lexicon with LexiconRecurse/LexiconMemRecurse: bit-identical to the same
    model hand-written in Fleet C++ (tests/native/lex_driver.cpp)."""
    steps = 100_000
    rows, traj = _native(_driver("lex_driver"), seed, steps, 5)
    res = lexa.run(LA.DATA, fl.MCMCChain(), control=fl.Control(steps=steps, seed=seed), top=5)
    assert [(h.posterior, h.prior, h.string()) for h in res.top] == \
        [(float.fromhex(a), float.fromhex(b), c) for a, b, c in rows]
    names, seq = lexa.module.trace(lexa._convert_data(LA.DATA), steps, 0, seed, 64, True, True)
    assert _fnv(names[i] for i in seq) == traj
    assert any("F(%s,%s)" in n for n in names) and any("Fm(%s,%s)" in n for n in names)


@pytest.fixture(scope="session")
def num():
    return fl.build(NUM.MyHypothesis)


@pytest.mark.parametrize("seed", [17, 3])
def test_equivalence_number(num, seed):
    """Recursion, a recursion-penalty prior, same-format if rules, an opaque std::bitset type
    and a whole-dataset likelihood: bit-identical to Fleet's Models/Number-Simple."""
    steps = 100_000
    rows, traj = _native(_driver("number_driver"), seed, steps, 5)
    res = num.run([], fl.MCMCChain(), control=fl.Control(steps=steps, seed=seed), top=5)
    assert [(h.posterior, h.prior, h.serialize()) for h in res.top] == \
        [(float.fromhex(a), float.fromhex(b), c) for a, b, c, _ in rows]
    names, seq = num.module.trace([], steps, 0, seed, 64, True, True)
    assert _fnv(names[i] for i in seq) == traj
    assert any("recurse(%s)" in n for n in names)


def test_number_semantics(num):
    cp = num.parse("if(singleton(x),one,next(recurse(setdifference(x,select(x)))))")
    assert NUM.knower_level(cp) == "1.2.3.4.5.6.7.8.9.10"
    two = num.parse("if(singleton(x),one,if(doubleton(x),two,undef))")
    assert NUM.knower_level(two) == "1.2.U.U.U.U.U.U.U.U"
    sel = num.parse("if(singleton(select(x)),one,two)")
    assert sel.call(0b1011) == 1
    with pytest.raises(fl.Undefined):                 # select of the empty set
        num.parse("if(singleton(select(x)),one,two)").call(0)
    with pytest.raises(OverflowError):                # Set is an i32 newtype
        cp.call(1 << 40)
    assert C.roundtrip(num, 300)["failures"] == 0 and C.prior(num, 300)["max_rel_error"] < 1e-12


def test_opaque_roundtrip():
    """An opaque type crossing the boundary in both directions through its generated caster."""
    Set = fl.opaque("std::bitset<16>", "<bitset>", py=int,
                    from_py="std::bitset<16>((unsigned long long)v)", to_py="(int64_t)v.to_ullong()",
                    check=lambda v: 0 <= v < 2 ** 16)
    g = fl.Grammar(input=Set, output=Set)

    @g.primitive(cpp="x << 1")
    def shl(x: Set) -> Set: ...
    g.add_builtin(B.X)

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    m = fl.build(H)
    assert m.parse("shl(shl(x))").call(0b101) == 0b10100
    assert m.parse("shl(x)").call(1 << 15) == 0          # bits shift out of a bitset<16>
    with pytest.raises(ValueError):                      # 2^16 does not fit a bitset<16>
        m.parse("x").call(1 << 16)
    r = m.run([fl.Datum(3, 12), fl.Datum(1, 4)], control=fl.Control(steps=5000, seed=2), top=1)
    assert r.top[0].call(5) == 20


# --------------------------------------------------------------------------------------
# recursion semantics
# --------------------------------------------------------------------------------------

def test_recursion_semantics(rec):
    rev = rec.parse("if_s(isempty(x),Ø,append(F(tail(x)),head(x)))")
    assert rev.call("abc") == "cba" and rev.call("") == ""
    assert rev.call("ab" * 30) == "ba" * 30                      # 60 recursive calls: fine
    with pytest.raises(fl.Undefined):                           # 70 > MAX_RECURSE (64)
        rev.call("a" * 70)
    srev = rec.parse("append(SF(tail(x)),head(x))")              # SafeRecurse: "" -> ""
    assert srev.call("abcd") == "dcba" and srev.call("") == ""
    mrev = rec.parse("if_s(isempty(x),Ø,append(Fm(tail(x)),head(x)))")
    assert mrev.call("hello") == "olleh"


def test_vm_limits_change_semantics():
    class RecShallow(fl.DeterministicHypothesis, grammar=M.gs):
        likelihood = fl.likelihoods.exact()
        vm = fl.VMLimits(max_recurse=10)
    m = fl.build(RecShallow)
    rev = m.parse("if_s(isempty(x),Ø,append(F(tail(x)),head(x)))")
    assert rev.call("a" * 10) == "a" * 10
    with pytest.raises(fl.Undefined):
        rev.call("a" * 11)


def test_recursion_checks(rec):
    assert C.roundtrip(rec, 300)["failures"] == 0
    assert C.prior(rec, 300)["max_rel_error"] < 1e-12


# --------------------------------------------------------------------------------------
# stochastic
# --------------------------------------------------------------------------------------

def test_stochastic_distribution(coin):
    h = coin.parse("or(flip(),flip())")
    d = h.call(True)
    assert set(d) == {True, False}
    p = h.distribution(True)
    assert abs(p[True] - 0.75) < 1e-12 and abs(p[False] - 0.25) < 1e-12
    assert abs(h.logprob(True, False) - __import__("math").log(0.25)) < 1e-12
    assert coin.parse("flip()").distribution(True) == {False: 0.5, True: 0.5}


def test_distribution_is_stochastic_only(rec):
    with pytest.raises(TypeError):
        rec.parse("x").distribution("ab")
    with pytest.raises(TypeError):
        rec.parse("x").logprob("ab", "ab")


def test_stochastic_checks(coin):
    assert C.roundtrip(coin, 300)["failures"] == 0
    assert C.prior(coin, 300)["max_rel_error"] < 1e-12
    rep = C.posterior(coin, DATA_BOOL, max_nodes=4, steps=400_000, seed=3)
    assert rep["p_value"] > 1e-4


def test_insert_delete_proposals_target_the_posterior():
    """Fleet's insert/delete proposals (with their forward-backward terms) leave the exact
    posterior invariant on this space."""
    m = fl.build(M.CoinID)
    for seed in (3, 4):
        assert C.posterior(m, DATA_BOOL, max_nodes=4, steps=400_000, seed=seed)["p_value"] > 1e-4


def test_stochastic_transpiled_likelihood():
    m = fl.build(M.CoinT2)
    assert m.info["differential"]["likelihood"]["mismatches"] == 0


# --------------------------------------------------------------------------------------
# lexicon
# --------------------------------------------------------------------------------------

def test_lexicon_api(lexa):
    h = lexa.parse({0: "add(x,x)", 1: "inc(F(0,x))"})
    assert [h.call(x) for x in range(4)] == [1, 3, 5, 7]
    assert h.factors == {0: "λx.add(x,x)", 1: "λx.inc(F(0,x))"}
    assert lexa.deserialize(h.serialize()) == h
    hm = lexa.parse({0: "add(x,x)", 1: "inc(Fm(0,x))"})
    assert hm.call(5) == 11
    loop = lexa.parse({0: "F(1,x)", 1: "F(0,x)"})                # mutual recursion forever
    with pytest.raises(fl.Undefined):
        loop.call(1)
    with pytest.raises(ValueError):
        lexa.parse({0: "x"})                                    # missing key 1
    with pytest.raises(ValueError):
        lexa.parse({0: "x", 1: "x", 2: "x"})


def test_lexicon_checks(lexa):
    assert C.roundtrip(lexa, 300)["failures"] == 0
    assert C.prior(lexa, 300)["max_rel_error"] < 1e-12


@pytest.mark.parametrize("H", [M.Lex2, M.Lex2All])
def test_lexicon_exact_posterior(H):
    m = fl.build(H)
    rep = C.posterior(m, DATA_BOOL, max_nodes=3, steps=400_000, seed=5)
    assert rep["p_value"] > 1e-4


# --------------------------------------------------------------------------------------
# static checks
# --------------------------------------------------------------------------------------

def _grammar(*builtins, inp=bool, out=bool):
    g = fl.Grammar(input=inp, output=out)
    g.add_terminal("T", True) if out is bool else None
    g.add_builtin(B.X, *builtins)
    return g


def test_flip_needs_stochastic():
    g = _grammar(B.Flip)

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TypeError, match="stochastic"):
        fl.build(H)


def test_likelihood_kind_must_match():
    g = _grammar()

    class H1(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.stochastic_exact()

    class H2(fl.StochasticHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    for H in (H1, H2):
        with pytest.raises(TypeError):
            fl.build(H)


def test_recursion_kind_checks():
    g1 = _grammar(B.Recurse)

    class L1(fl.Lexicon, grammar=g1, keys=[0, 1], main=0):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TypeError, match="Lexicon"):
        fl.build(L1)
    g2 = _grammar(B.LexiconRecurse(key=int))

    class S2(fl.StochasticHypothesis, grammar=g2):
        likelihood = fl.likelihoods.stochastic_exact()
    with pytest.raises(TypeError, match="fl.Lexicon"):
        fl.build(S2)
    g3 = _grammar(B.LexiconRecurse(key=str))

    class L3(fl.Lexicon, grammar=g3, keys=[0, 1], main=0):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TypeError, match="key type"):
        fl.build(L3)
    with pytest.raises(TypeError):
        B.LexiconRecurse(key=float)
    with pytest.raises(ValueError):
        class L4(fl.Lexicon, grammar=g1, keys=[0, 1], main=2):
            likelihood = fl.likelihoods.exact()


def test_safe_recurse_needs_sized_input():
    g = fl.Grammar(input=int, output=int)
    g.add_builtin(B.X, B.SafeRecurse)

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TypeError, match="size"):
        fl.build(H)


def test_lexicon_key_type_must_not_mix():
    g = fl.Grammar(input=fl.i32, output=fl.i32)
    g.add_builtin(B.X, B.LexiconRecurse(key=int))

    @g.primitive(cpp="a + 1")
    def inc(a: fl.i32) -> fl.i32: ...

    class L(fl.Lexicon, grammar=g, keys=[0, 1], main=0):
        likelihood = fl.likelihoods.exact()
    with pytest.raises(TypeError, match="mix"):
        fl.build(L)


def test_same_format_different_types():
    """Fleet's Number model reuses "if(%s,%s,%s)" for two types; parse picks by type."""
    g = fl.Grammar(input=int, output=int, types=[int, bool])

    @g.primitive(cpp="a == 0")
    def iszero(a: int) -> bool: ...
    g.add_terminal("one", 1)
    g.add_builtin(B.X, B.If(int), B.If(bool))

    class H(fl.DeterministicHypothesis, grammar=g):
        likelihood = fl.likelihoods.exact()
    m = fl.build(H)
    h = m.parse("if(if(iszero(x),iszero(one),iszero(x)),one,x)")
    assert h.call(0) == 0 and h.call(5) == 5
    assert C.roundtrip(m, 200)["failures"] == 0 and C.prior(m, 200)["max_rel_error"] < 1e-12


# --------------------------------------------------------------------------------------
# eject
# --------------------------------------------------------------------------------------

def _make(d):
    import re
    from fleetpy.build import fleet_root, flags
    cmd = ["make", "-s", "-C", str(d), "all"]
    if flags("release")[2]:
        ff = re.search(r"^FLEET_FLAGS\s*=(.*)$", (fleet_root() / "Fleet.mk").read_text(), re.M).group(1)
        std = [f for f in flags("release")[0] if f.startswith("-std=")][0]
        cmd.append("FLEET_FLAGS=" + re.sub(r"-std=\S+", std, ff))
    p = subprocess.run(cmd, capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr


@pytest.mark.parametrize("which", ["flt", "lex"])
def test_eject_new_kinds(tmp_path, which, flt, lexa):
    m, data, expect = (flt, FLT.DATA, "λx.") if which == "flt" else (lexa, LA.DATA, "F(1,x):=")
    d = m.eject(tmp_path / which, data, fl.MCMCChain())
    _make(d)
    out = subprocess.run([str(d / "main"), "--steps=5000", "--seed=5", "--top=3", "--header=0"],
                         capture_output=True, text=True, check=True).stdout
    assert expect in out
