# nohup python3 comparisons.py | shuf | parallel --line-buffer > o.txt &

# No shuf or else LZ gets lost
nohup python3 comparisons.py | parallel --line-buffer > o.txt &
