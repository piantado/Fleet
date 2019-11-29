

nohup /usr/bin/time --output out/time ./main --time=12h --chains=8 --threads=16 --restart=0 --thin=0 --top=1000 > out/out.txt 2> out/err.txt & 

