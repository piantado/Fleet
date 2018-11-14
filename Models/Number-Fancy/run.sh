

nohup /usr/bin/time --output out/output.time ./main --mcmc=100000 --threads=10 --restart=25000 --thin=0 --top=1000 > out/output.txt 2> out/output.err & 

