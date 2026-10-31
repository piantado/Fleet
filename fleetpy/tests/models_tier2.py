"""A model made only of tier-2 primitives that exercise the semantic edges of the transpiler."""

import math

import fleetpy as fl
from fleetpy import Undefined


@fl.struct
class P:
    a: fl.i8
    b: float
    name: str


g = fl.Grammar(input=int, output=int)


@g.primitive
def fdiv(a: int, b: int) -> int:
    return a // b                   # floor division toward -inf, ZeroDivisionError -> undefined


@g.primitive
def pmod(a: int, b: int) -> int:
    return a % b                    # sign of divisor


@g.primitive
def addc(a: int, b: int) -> int:
    return a + b                    # int64 overflow -> undefined


@g.primitive
def mulc(a: int, b: int) -> int:
    return a * b - 1


@g.primitive
def neg(a: int) -> int:
    return -a


@g.primitive
def to8(a: int) -> fl.i8:
    return a                        # narrowing: out of [-128,127] -> undefined


@g.primitive
def sq(a: int) -> int:
    return a ** 2


@g.primitive
def pick(a: int, b: int) -> int:
    if a > b:
        r = a - b
    elif a == b:
        raise Undefined
    else:
        r = b - a
    return r


@g.primitive
def triangle(n: int) -> int:
    s = 0
    for i in range(n):
        s += i
    return s


@g.primitive
def stepsum(a: int, b: int) -> int:
    s = 0
    for i in range(a, b, -3):
        s = s + i
    return s


@g.primitive
def collatz(n: int) -> int:
    steps = 0
    for k in range(200):
        if n == 1:
            break
        if n % 2 == 0:
            n2 = n // 2
        else:
            n2 = 3 * n + 1
        steps += 1
        return steps + n2 - n2
    return steps


@g.primitive
def fl_ops(a: int, b: int) -> int:
    x = a / b                       # true division; ZeroDivisionError -> undefined
    y = math.sqrt(abs(x)) + math.log1p(abs(x))
    return math.floor(y * 1000.0)


@g.primitive
def fexp(a: int) -> int:
    return math.floor(math.exp(float(a)) )   # OverflowError -> undefined


@g.primitive
def tr(a: int) -> int:
    return int(float(a) / 3.0)      # truncation toward zero


@g.primitive
def minmax(a: int, b: int) -> int:
    return max(a, b) - min(a, b)


@g.primitive
def strstuff(a: int) -> int:
    s = "日本" + "ab"
    t = s[a] if a < 4 else "z"      # code-point indexing; IndexError -> undefined
    return len(s) * 10 + len(t) + (1 if t < "b" else 0)


@g.primitive
def lists(a: int) -> int:
    xs = [a, a + 1, a + 2]
    ys: list[int] = []
    zs = xs + ys + [7]
    return zs[-1] + zs[a] + len(zs)  # negative and out-of-range indexing


@g.primitive
def mkP(a: int) -> int:
    p = P(a, float(a) / 2.0, "n")   # struct construction; a is narrowed to i8
    q = p.b * 2.0 if p.name == "n" else -1.0
    return math.floor(q) + p.a + fdiv(a, 3)   # calls another tier-2 primitive


@g.primitive
def fallthrough(a: int) -> int:
    if a > 0:
        return a


@g.primitive
def booly(a: int) -> bool:
    return a > 3 and not (a == 5) or a < -100


@g.primitive
def ifb(c: bool, a: int, b: int) -> int:
    return a if c else b


SMALL_PRIMES = (2, 3, 5, 7, 11)
GREETING = "héllo"


@g.primitive
def bits(a: int, b: int) -> int:
    """bitwise ops and shifts: Python semantics on negatives, shift ranges, overflow"""
    return ((a & b) ^ (a | ~b)) + (a << (b % 70)) - (a >> (b % 70)) + a.bit_count() * b.bit_length()


@g.primitive
def slices(a: int, b: int) -> int:
    """str and list slicing with negative/out-of-range bounds (code points, Python clamping)"""
    s = GREETING[a % 9 - 4:b % 9 - 4] + GREETING[:a % 7] + GREETING[b % 5:]
    xs = [a, b, a + 1, b + 1, 7]
    ys = xs[a % 11 - 5:b % 11 - 5] + xs[:-1]
    return len(s) * 100 + len(ys) * 10 + (ys[0] if len(ys) > 0 else -1)


@g.primitive
def consts(a: int) -> int:
    """module-level constants, including a tuple"""
    return SMALL_PRIMES[a % 5] * len(GREETING)


@g.primitive
def zero() -> int:
    return 0


@g.primitive
def one() -> int:
    return 1


g.add_builtin(fl.builtins.X)


class H(fl.DeterministicHypothesis, grammar=g):
    likelihood = fl.likelihoods.exact()


class H4(fl.DeterministicHypothesis, grammar=g):
    """A likelihood whose int arithmetic overflows int64 for most outputs: the compiled code
    makes those cases undefined (-inf), and so must the reference it is tested against."""
    @fl.likelihood
    def lik(self, di) -> float:
        y = self.call(di.input)
        big = y * 4611686018427387904       # 2^62
        return -float(abs(big - di.output) % 7)


@fl.struct
class Q:
    v: float


g2 = fl.Grammar(input=float, output=float)


@g2.primitive
def fadd(a: float, b: float) -> float:
    return a + b


@g2.primitive
def fmul(a: float, b: float) -> float:
    return a * b


@g2.primitive
def fdivf(a: float, b: float) -> float:
    return a / b


@g2.primitive
def fpow3(a: float) -> float:
    return a ** 3


@g2.primitive
def fhyp(a: float, b: float) -> float:
    return math.sqrt(a * a + b * b) + math.atan2(a, b)


@g2.primitive
def flog(a: float) -> float:
    return math.log(a)


@g2.primitive
def fmin(a: float, b: float) -> float:
    return min(a, b)


@g2.primitive
def fcmp(a: float, b: float) -> float:
    return 1.0 if a <= b else 0.0


@g2.primitive
def half() -> float:
    return 0.5


g3 = fl.Grammar(input=str, output=str, types=[str, fl.char])


@g3.primitive
def push(s: str, c: fl.char) -> str:
    return s + c + str(c)


@g3.primitive
def rot(s: str) -> str:
    return s[1:] + s[:1]


@g3.primitive
def cmp(s: str, c: fl.char) -> str:
    return s[-1:] if c < "m" and len(s) > 0 else s[:-1]


g3.add_terminal("'q'", "q", fl.char)
g3.add_builtin(fl.builtins.X)


class H3(fl.DeterministicHypothesis, grammar=g3):
    likelihood = fl.likelihoods.exact()


g2.add_builtin(fl.builtins.X)


class H2(fl.DeterministicHypothesis, grammar=g2):
    @fl.likelihood
    def lik(self, di) -> float:
        y = self.call(di.input)
        d = y - di.output
        return -d * d / 2.0 - math.log(2.0 * math.pi) / 2.0
