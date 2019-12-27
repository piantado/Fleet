#!/bin/bash

myhost=$(hostname -s)
threads=2 
top=1000

# make a copy here so that when we sync/recompile
# we won't change this
mymain=run/main.$myhost
cp main $mymain
cp Main.cpp run/Main.cpp.$myhost # for a record 


if [ $myhost = "colala-hastings" ] ; then
    factors=(1 4)
    jobs=30
    parameters=Parameters/LongRun.csv
elif [ $myhost = "colala-metropolis" ] ; then
    factors=(2 3)
    jobs=30
    parameters=Parameters/LongRun.csv
elif [ $myhost = "simon" ] ; then
    factors=(1 2 3 4)
    jobs=70
    parameters=Parameters/ShortRun.csv
fi

rm -f run/parameters.$myhost
for f in "${factors[@]}" 
do
# The parameters file does not have a number of factors
# so we are going to ADD a column for each of myfactors
    cat $parameters | sed "s/$/,$f/" >> run/parameters.$myhost  
done

cat run/parameters.$myhost | parallel --jobs=$jobs --joblog run/log.$myhost --csv --colsep ',' \
        /usr/bin/time --output=out/{1}-{5}.time --verbose \
        ./$mymain "${GARGS[@]}" --input=data/{1} --alphabet=\"{2}\" --time={3} --nfactors={5} --threads=$threads --thin=0 --mcmc=0 --top=$top --restart=100000 ">" out/{1}-{5}.out "2>" out/{1}-{5}.err
