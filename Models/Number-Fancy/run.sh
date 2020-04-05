

nohup /usr/bin/time --output out/time ./main --time=1d --chains=8 --threads=16 --restart=0 --thin=0 --top=50000 > out/out.txt 2> out/err.txt & 

