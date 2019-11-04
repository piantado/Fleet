#!/bin/bash

myhost=$(hostname -s)
threads=1 # for running on simon and garfunkel, multithreads don't seem to go well
jobs=70 # on the other hand, we can have lots of jobs

factors=(1 3)
if [ $myhost = "colala-hastings" ] || [ $myhost = "garfunkel" ] ; then
    factors=(2 4)
fi

# make a copy here so that when we sync/recompile
# we won't change this
mymain=main.$myhost
cp main $mymain

# The parameters file does not have a number of factors
# so we are going to ADD a column for each of myfactors
{ cat Parameters.csv | sed "s/$/,${factors[0]}/" ; 
  cat Parameters.csv | sed "s/$/,${factors[1]}/" ; } | 
        parallel --jobs=$jobs --joblog log.$myhost --csv --colsep ',' \
        /usr/bin/time --output=out/{1}-{5}.time --verbose \
        ./$mymain "${GARGS[@]}" --input=data/{1} --alphabet=\"{2}\" --time={3} --nfactors={5} --threads=$threads --thin=0 --mcmc=0 --top=10 --restart=100000 ">" out/{1}-{5}.out "2>" out/{1}-{5}.err
