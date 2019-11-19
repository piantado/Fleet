

nohup /usr/bin/time --output out/output.time ./main --time=1d --chains=8 --threads=16 --restart=0 --thin=0 --top=1000 > out/output.out 2> out/output.err & 

