import itertools
import numpy
import random
from LOTlib.Miscellaneous import partitions
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar
from random import random

class ChineseNumeral(FormalLanguage):
    def __init__(self):
        pass
    
    def sample_string(self):
        l = numpy.random.geometric(p=1./3.)
        out = ""
        oldn = 0
        for i in range(l):
            n = numpy.random.geometric(p=1./3.) + oldn
            out = 'a' + 'b'*n + out
            oldn = n
        return out
            
    def terminals(self):
        return list('ab')

    
# just for testing
if __name__ == '__main__':
    language = ChineseNumeral()
    print language.sample_data(10000)
