
nice -n -10 ./main --inner-time=1s --runtype=hypotheses --alphabet=abcd --hypotheses=hypotheses/1s --output=hypotheses/1s

nice -n -10 ./main --inner-time=1m --runtype=hypotheses --alphabet=abcd --hypotheses=hypotheses/1m --output=hypotheses/1m

nice -n -10 ./main --inner-time=5m --runtype=hypotheses --alphabet=abcd --hypotheses=hypotheses/5m --output=hypotheses/5m

nice -n -10 ./main --inner-time=10m --runtype=hypotheses --alphabet=abcd --hypotheses=hypotheses/10m --output=hypotheses/10m

nice -n -10 ./main --inner-time=1h --runtype=hypotheses --alphabet=abcd --hypotheses=hypotheses/1h --output=hypotheses/1h
