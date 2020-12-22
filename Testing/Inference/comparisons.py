# a little python script to output commands for use with parallel

import itertools

times = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]; # minutes # [1, 5, 10, 50, 100, 500, 1000]
chains = [1, 2, 5, 10, 20] #, 200, 500]
replications = range(20)
restart = [0, 1000, 10000]
inner_times = ["10", "100", "1000", "5000", "10000"] # measured in q

# some methods don't need chains:
methods = ['prior-sampling', 'enumeration', 'beam']
# some do:
chainy_methods = ['parallel-tempering', 'chain-pool']
# and do mcts
# mcts_methods = ['mcmc-within-mcts', 'prior-sample-mcts'] # 'full-mcts'

datas = {
    "sort":"734:347,1987:1789,113322:112233,679:679,214:124,9142385670:0123456789",
    "max":"734:7,1987:9,113322:3,679:9,214:4,9142385670:9",
    "last":"734:4,1987:7,113322:2,679:9,214:4,9142385670:0",
    "insert-ones":"734:713141,1287:11218171,113322:111131312121,678:617181,214:211141,14235670:1141213151617101",
    "insert-first":"734:773747,1287:11218171,113322:111131312121,678:667686,214:221242,4235670:44243454647404",
    "odd-ones":"33321:1,332121:0,734:0,1287:1,113322:0,678:0,214:1,111:1,1411421:0",
    "count":"1:1,2:22,3:333,4:4444",
    "run-length":"13:3,25:55,32:222,44:4444,52:22222",
    "sum":"13:4,25:7,32:5,44:8,52:7",
    "copycat-1":"123:124,555:556",
    "copycat-2":"123:234,555:666"
}


ex = "./main --top=1 --threads=1 --header=0 "

for i,t,dk in itertools.product(replications, times, datas.keys()):
    
    for r in restart:
        # Run all the other methods
        for m in methods:
            prefix = "%s\t%s\t%s\t%s\t%s\t%s\t%s\t" % (i,t,m,r,dk,1,"NA")        
            print(ex, "--time=%sm"%t, "--method=%s"%m, "--restart=%s"%r, "--data=\"%s\""%datas[dk], "--chains=1", "--prefix=$\"%s\""%prefix)
        
        for m in chainy_methods:
            for c in chains:
                prefix = "%s\t%s\t%s\t%s\t%s\t%s\t%s\t" % (i,t,m,r,dk,c,"NA")        
                print(ex, "--time=%sm"%t, "--method=%s"%m, "--restart=%s"%r, "--data=\"%s\""%datas[dk], "--chains=%s"%c, "--prefix=$\"%s\""%prefix)
        
        for it, c in itertools.product(inner_times, chains):
            m = 'mcmc-within-mcts'
            prefix = "%s\t%s\t%s\t%s\t%s\t%s\t%s\t" % (i,t,m,r,dk,c,it)      
            # NOTE: This has to be inner-restart or else it isn't used 
            print(ex, "--time=%sm"%t, "--method=%s"%m, "--inner-restart=%s"%r, "--data=\"%s\""%datas[dk],  "--chains=%s"%c, "--prefix=$\"%s\""%prefix, "--inner-time=%sq"%it)
    
    for it in itertools.product(inner_times):
        m = 'prior-sample-mcts' # no restart, chains, or inner-times
        prefix = "%s\t%s\t%s\t%s\t%s\t%s\t%s\t" % (i,t,m,0,dk,0,it)      
        # NOTE: This has to be inner-restart or else it isn't used 
        print(ex, "--time=%sm"%t, "--method=%s"%m, "--data=\"%s\""%datas[dk],  "--chains=%s"%c, "--prefix=$\"%s\""%prefix, "--inner-time=%sq"%it)
        
