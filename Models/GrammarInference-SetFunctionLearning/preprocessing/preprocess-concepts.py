# A simple script to convert to a friendlier format for C++

import re
import os
import pandas
from collections import defaultdict


CONCEPT_DIR = "concepts/"
HUMAN_FILE = "TurkData-Accuracy.txt"
MAX_SET = 25 # the largest set number we tested

## First read and aggregate the human data
humanfile = pandas.read_csv(HUMAN_FILE, sep="\t", header=0, index_col=False)
human = defaultdict(int) # just counts -- indexed by (concept, list, item, response, true/false)
for _, r in humanfile.iterrows():
    tup = (r['concept'], r['list'], r['set.number'], r['response.number'], r['response']=="T")
    human[tup] += 1
    # print(tup)

## Now read and aggregate the training data

for filename in os.listdir(CONCEPT_DIR):
    if re.search(r"L3|L4", filename): continue # skip these lists

    with open(CONCEPT_DIR+filename) as f:
        lines = f.readlines()

        m = re.match("CONCEPT_(.+)__LIST_(.+)\.", filename)
        concept, list = m.groups()

        target = lines[0].strip()
        setNumber = 1
        for l in lines[1:]:
            l = l.strip()
            parts = re.split(r"\t", l)

            vals = re.findall(r"\#f|\#t", parts[0]) # get the trues and falses
            assert(len(vals) == len(parts[1:]))

            responseNumber = 1
            for v, o in zip(vals, parts[1:]):

                features = re.split(r",", o)

                hyes = human[ (concept, list, setNumber, responseNumber, True)]
                hno  = human[ (concept, list, setNumber, responseNumber, False)]

                print(concept, list, setNumber, responseNumber, v == '#t', *(features[0:3]), hyes, hno)
                responseNumber += 1
            setNumber += 1
            if(setNumber > MAX_SET): break
