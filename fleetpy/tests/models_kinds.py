"""Small models for testing stochastic hypotheses, recursion, memoization and lexicons."""

import math

import fleetpy as fl
from fleetpy import builtins as B

# ---- deterministic recursion over strings ----------------------------------------------
gs = fl.Grammar(input=str, output=str, types=[str, bool])


@gs.primitive(fmt="Ø", cpp='std::string("")')
def empty() -> str: ...


@gs.primitive(cpp="return s.empty() ? std::string() : s.substr(1);")
def tail(s: str) -> str: ...


@gs.primitive(cpp="return s.empty() ? std::string() : std::string(1, s.at(0));")
def head(s: str) -> str: ...


@gs.primitive(cpp="if(a.size() + b.size() > 256) throw VMSRuntimeError();\nreturn a + b;")
def append(a: str, b: str) -> str: ...


@gs.primitive(cpp="s.empty()")
def isempty(s: str) -> bool: ...


gs.add_builtin(B.X(p=2.0), B.If(str, fmt="if_s(%s,%s,%s)"), B.Recurse(fmt="F(%s)"),
               B.SafeRecurse(fmt="SF(%s)"), B.MemRecurse(fmt="Fm(%s)"))


class Rec(fl.DeterministicHypothesis, grammar=gs):
    likelihood = fl.likelihoods.exact()


# ---- a small stochastic model over bools ------------------------------------------------
gb = fl.Grammar(input=bool, output=bool)
gb.add_terminal("T", True)
gb.add_terminal("F", False)
gb.add_builtin(B.Flip, B.And, B.Or, B.Not, B.X)


class Coin(fl.StochasticHypothesis, grammar=gb):
    likelihood = fl.likelihoods.stochastic_exact()


class CoinID(fl.StochasticHypothesis, grammar=gb):
    """Same model, with Fleet's insert/delete proposals (checks their detailed balance)."""
    likelihood = fl.likelihoods.stochastic_exact()
    proposal = fl.proposals.regenerate_insert_delete(regenerate=0.5)


class CoinT2(fl.StochasticHypothesis, grammar=gb):
    """Same model, with a transpiled likelihood (a tempered version of stochastic_exact)."""
    @fl.likelihood(nonpositive=True)
    def lik(self, di) -> float:
        lp = self.logprob(di.input, di.output)
        if math.isinf(lp):
            return lp
        return lp * di.count


# ---- a lexicon over bools, stochastic factors -------------------------------------------
gl = fl.Grammar(input=bool, output=bool)
gl.add_terminal("T", True)
gl.add_builtin(B.Flip, B.Not, B.X, B.LexiconRecurse(key=str, fmt="F(%s,%s)"))


class Lex2(fl.Lexicon, grammar=gl, keys=["a", "b"], main="b", stochastic=True):
    likelihood = fl.likelihoods.stochastic_exact()


class Lex2All(fl.Lexicon, grammar=gl, keys=["a", "b"], main="b", stochastic=True,
              require_all_called=True):
    likelihood = fl.likelihoods.stochastic_exact()
