

nohup /usr/bin/time --output out/time ./main --time=5d --chains=16 --threads=16 --restart=0 --thin=0 --top=10000 > out/out.txt 2> out/err.txt & 

