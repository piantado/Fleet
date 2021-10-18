# a little python script to output commands for use with parallel
from copy import copy
import itertools

times = [1, 5, 10, 30, 60, 120, 250, 500] #, 500, 1000, 3000, 5000, 10000]; # seconds # [1, 5, 10, 50, 100, 500, 1000]
chains = [1, 5, 10, 50] 
replications = range(100)
restart = [0, 1000, 10000]
inner_times = ["100", "1000"] # measured in q
explores = [0.1, 1.0, 10.0, 100.0]
partition_depths = [1,2,3,4]

datas = {
    "sort":"734:347,1987:1789,113322:112233,679:679,214:124,9142385670:0123456789",
    "max":"734:7,1987:9,113322:3,679:9,214:4,9142385670:9",
    "last":"734:4,1987:7,113322:2,679:9,214:4,9142385670:0",
    "insert-ones":"734:713141,1287:11218171,113322:111131312121,678:617181,214:211141,14235670:1141213151617101",
    "insert-first":"734:773747,1287:11218171,113322:111131312121,678:667686,214:221242,4235670:44243454647404",
    "odd-ones":"33321:1,332121:0,734:0,1287:1,113322:0,678:0,214:1,111:1,1411421:0",
    "count":"1:1,2:22,3:333,4:4444",
    "alternate":"1234:13,5674:57,987654321:97531",
    "firstlast":"734:74,1287:17,113322:12,678:68,214:24,4235670:40",
    "run-length":"13:3,25:55,32:222,44:4444,52:22222",
    "sum":"13:4,25:7,32:5,44:8,52:7"
    #"copycat-1":"123:124,555:556",
    #"copycat-2":"123:234,555:666"
}


ex = "../../Models/Sorting/main "

defaults = {'time':'1m',
			'method':'chain-pool',
			'restart':'0',
			'data':'NA',
			'chains':'1',
			'header':'0',
			'top':'1',
			'threads':'1',
			'inner_time':'1',
			'explore':'1.0',
			'header':'0',
			'partition_depth':'1'}

def runplease(it, **d):
	# any arguments we given in d override the defaults:
	q = copy(defaults)
	q.update(d)

	# fix the underscores:
	q['inner-time'] = q['inner_time']
	del q['inner_time']
	q['partition-depth'] = q['partition_depth']
	del q['partition_depth']

	# make everything strings
	q = {str(k):str(v) for k,v in q.items()}

	# get the prefix
	q['prefix'] = "\""+'\t'.join([str(it), q['time'], q['method'], q['restart'],
							      q['data'], q['chains'], q['inner-time'], q['explore'], q['partition-depth'] ])+'\t'+"\""

	# Add units (so they aren't in the prefix)
	q['time'] += 's'
	q['inner-time'] += 'q'

	# and fix the dat aargument
	q['data'] = datas[q['data']]

	print(ex, ' '.join('--'+k+'='+v for k,v in q.items()))

for i in replications:

	for d,t in itertools.product(datas.keys(), times):

		# Enumeration only needs to run once since they're deterministic:
		if i == 0:  
			runplease(0, method='basic-enumeration', time=t, data=d)
			runplease(0, method='partial-LZ-enumeration', time=t, data=d)
			runplease(0, method='full-LZ-enumeration', time=t, data=d)
		
		runplease(i, method='hill-climbing', time=t, data=d)

		runplease(i, method='prior-sampling', time=t, data=d)

		runplease(i, method='beam', time=t, data=d)

		for r,c in itertools.product(restart, chains):
			runplease(i, method='parallel-tempering', time=t, restart=r, data=d, chains=c)
			#runplease(i, method='parallel-tempering-ID', time=t, restart=r, data=d, chains=c)
			#runplease(i, method='parallel-tempering-prior-propose', time=t, restart=r, data=d, chains=c)
			
		for r,c in itertools.product(restart, chains):
			runplease(i, method='chain-pool', time=t, restart=r, data=d, chains=c)

		for r,pd in itertools.product(restart, partition_depths):
			runplease(i, method='partition-mcmc', time=t, restart=r, data=d, partition_depth=pd)

		for it,c,r,e in itertools.product(inner_times, chains, restart, explores):
			runplease(i, method='mcmc-within-mcts', inner_time=it, chains=c, restart=r, time=t, data=d, explore=e)

		for it,r,e in itertools.product(inner_times, restart, explores):
			runplease(i, method='prior-sample-mcts', inner_time=it, restart=r, time=t, data=d, explore=e)

