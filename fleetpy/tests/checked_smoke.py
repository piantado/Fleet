"""Run under fleetpy.check.run_checked: builds models of every kind with ASan+UBSan and exercises them."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "examples"))
import fleetpy as fl
import models_tier2 as M
import models_kinds as K
import formal_language as FLT
import lexicon_arith as LA
import number as NUM

DB = [fl.Datum(True, True), fl.Datum(True, False)]
for H, data in ((M.H, [fl.Datum(3, 6), fl.Datum(5, 10)]),
                (M.H3, [fl.Datum("ab", "ba")]),
                (M.H2, [fl.Datum(1.0, 2.0), fl.Datum(2.0, 4.0)]),
                (FLT.MyHypothesis, FLT.DATA),
                (LA.Lex, LA.DATA),
                (NUM.MyHypothesis, []),
                (K.Rec, [fl.Datum("abc", "cba"), fl.Datum("ab", "ba")]),
                (K.Coin, DB), (K.CoinID, DB), (K.CoinT2, DB),
                (K.Lex2, DB), (K.Lex2All, DB)):
    m = fl.build(H)  # FLEETPY_MODE=checked; tier-2 models also run their differential tests here
    r = m.run(data, fl.ChainPool(2), control=fl.Control(steps=8000, threads=2, seed=3), top=5)
    for h in r.top:                       # copies of hypotheses (lexicon loaders!) used after the run
        h2 = m.deserialize(h.serialize())
        h2.compute_posterior(data)
        x = data[0].input if data else NUM.make_set(3)
        h.call(x) if not H.stochastic else h.distribution(x)
    m2 = m.run(data, fl.ParallelTempering(3, 2.0), control=fl.Control(steps=6000, threads=1, seed=4), top=3)
    print(H.__name__, r.top[0] if r.top else None, r.stats.samples, m2.stats.samples)

lex = fl.build(LA.Lex)
loop = lex.parse({0: "F(1,x)", 1: "F(0,x)"})
try:
    loop.call(1)
except fl.Undefined:
    pass
rec = fl.build(K.Rec)
try:
    rec.parse("if_s(isempty(x),Ø,append(F(tail(x)),head(x)))").call("a" * 100)
except fl.Undefined:
    pass
print("checked-ok")
