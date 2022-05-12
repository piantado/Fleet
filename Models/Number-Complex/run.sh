
for T in 1m 10m 1h 8h 1d 
do
    nohup /usr/bin/time --output out/time ./main --time=$T --threads=8 --chains=25 --restart=1000000 --top=10000 > out/out-$T.txt 2> out/err-$T.txt & 
done

    
