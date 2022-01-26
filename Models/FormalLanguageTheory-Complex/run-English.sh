TIME=7d
THREADS=15
CHAINS=15
top=100
prN=100

myhost=$(hostname -s)

if [ $myhost = "colala-hastings" ] ; then
   factors=(1 3 5)
elif [ $myhost = "colala-metropolis" ] ; then
   factors=(2 4 6)
fi
# factors=(1 2 3 4 5)

for f in "${factors[@]}" 
do
    /usr/bin/time --output=out-English/English-$f.time --verbose ./main --long-output --input=data/English --alphabet="dnavtp" --time=$TIME --nfactors=$f --chains=$CHAINS --threads=$THREADS --thin=0 --top=$top --prN=$prN > out-English/English-$f.out 2> out-English/English-$f.err &    

done

        
