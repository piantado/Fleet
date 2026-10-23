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

def as_fleet_array(lst, sep=','):
    return '['+sep.join(map(str,lst))+']'

## Now read and aggregate the training data
for filename in os.listdir(CONCEPT_DIR):
    if re.search(r"L3|L4", filename): continue # skip these lists

    with open(CONCEPT_DIR+filename) as f:
        lines = f.readlines()

        m = re.match(r"CONCEPT_(.+)__LIST_(.+)\.", filename)
        concept, list = m.groups()

        target = lines[0].strip()


        #if(re.search(r"forall|exists", target)):
        #    continue

        print("# Keeping concept", target)

        setNumber = 1
        condition_sets = []
        for l in lines[1:]:
            l = l.strip()
            parts = re.split(r"\t", l)

            vals = re.findall(r"\#f|\#t", parts[0]) # get the trues and falses
            assert(len(vals) == len(parts[1:]))

            # construct into a format for  C++ to load
            objs = []
            yes = []
            no = []
            response_number = 1
            for v, o in zip(vals, parts[1:]):

                # write in Fleet format for objects
                features = re.split(r",", o)
                objs.append('-'.join(features[0:3]))

                y = human[ (concept, list, setNumber, response_number, True)]
                n = human[ (concept, list, setNumber, response_number, False)]
                yes.append(y)
                no.append(n)
                response_number += 1

            assert len(objs) == len(yes) == len(no)

            this_set = as_fleet_array(objs)

            print( concept,
                   list,
                   setNumber,
                   as_fleet_array(condition_sets, ';'),
                   this_set,
                   as_fleet_array(yes),
                   as_fleet_array(no),
                   sep="\t" )

            # reformat valus to bool
            vals = ["1" if x == '#t' else "0" for x in vals]

            condition_sets.append("%s:%s"%(this_set, "[" + ",".join(vals)+"]" ))

            setNumber += 1
            if(setNumber > MAX_SET):
                break
