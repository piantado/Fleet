#!/bin/bash

myhost=$(hostname -s)
threads=1
top=1000

# make a copy here so that when we sync/recompile
# we won't change this
mymain=run/main.$myhost
cp main $mymain
cp Main.cpp run/Main.cpp.$myhost # for a record 

# parameters=AllParameters.csv
parameters=AllParameters-SmallRun.csv
  
if [ $myhost = "colala-hastings" ] ; then
    factors=(3 6)
    jobs=40
elif [ $myhost = "colala-metropolis" ] ; then
    factors=(2 5)
    jobs=40
elif [ $myhost = "simon" ] ; then
    factors=(1 4)
    jobs=75
fi
# elif [ $myhost = "garfunkel" ] ; then
#     factors=(2,6)
#     jobs=70
#     parameters=AllParameters.csv
# fi

rm -f run/parameters.$myhost
for f in "${factors[@]}" 
do
# The parameters file does not have a number of factors
# so we are going to ADD a column for each of myfactors
    cat $parameters | sed "s/$/,$f/" >> run/parameters.$myhost  
done

cat run/parameters.$myhost | parallel --jobs=$jobs --joblog run/log.$myhost --csv --colsep ',' \
        /usr/bin/time --output=out/{1}-{7}.time --verbose \
        ./$mymain "${GARGS[@]}" --input=data/{1} --alphabet=\"{2}\" --threads={3} --time={4} --nfactors={7} --thin=0 --mcmc=0 --top=$top --restart=100000 ">" out/{1}-{7}.out "2>" out/{1}-{7}.err
 
