#!/bin/bash

myhost=$(hostname -s)
top=500

# make a copy here so that when we sync/recompile
# we won't change this
mymain=run/main.$myhost
cp main $mymain
cp Main.cpp run/Main.cpp.$myhost # for a record 

parameters=AllParameters-Short.csv


#if [ $myhost = "simon" ] ; then
#    factors=(1 3 5)
#    jobs=140
#elif [ $myhost = "garfunkel" ] ; then
#    factors=(2 4 6)
#    jobs=140
#fi
factors=(1 2 3 4)
jobs=100

rm -f run/parameters-short.$myhost
for f in "${factors[@]}" 
do
    # The parameters file does not have a number of factors
    # so we are going to ADD a column for each of myfactors
    cat $parameters | sed "s/$/,$f/" >> run/parameters-short.$myhost  
done

cat run/parameters-short.$myhost | parallel --jobs=$jobs --joblog run/log.$myhost --csv --colsep ',' \
        /usr/bin/time --output=out/{1}-{8}.time --verbose \
        ./$mymain "${GARGS[@]}" --input=data/{1} --alphabet=\"{2}\" --threads={3} --time={4} --nfactors={8} --thin=0 --top=$top --restart=100000 --maxlength={7} ">" out/{1}-{8}.out "2>" out/{1}-{8}.err
 
