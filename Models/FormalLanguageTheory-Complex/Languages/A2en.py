import numpy
from FormalLanguage import FormalLanguage
from random import random

class A2en(FormalLanguage):
    # A^(2^n)

    def __init__(self):
        self.grammar = None
        
    def terminals(self):
        return list('a')
    
    def sample_string(self):
        
        n = numpy.random.geometric(0.8)-1
            
        return 'a'*(2**n)



# just for testing
if __name__ == '__main__':
    language = A2en()
    print language.sample_data(100000)
