import sys 
import operator
from Languages import *

# just for testing
if __name__ == '__main__':
   
    d = int(sys.argv[1])
    l = sys.argv[2]
    language = eval(l+"()")
    data = language.sample_data(d)

    # print data
    for r in sorted(data[0].output.items(), key=operator.itemgetter(1), reverse=True):
        print "%s\t%i\n" % r,
