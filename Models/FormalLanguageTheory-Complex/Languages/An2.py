import numpy 
from FormalLanguage import FormalLanguage
from random import random

class An2(FormalLanguage):
    # A^(n^2)

    def __init__(self):
        self.grammar = None
        
    def terminals(self):
        return list('a')
    
    def sample_string(self):
        n = numpy.random.geometric(0.5)
            
        return 'a'*(n**2)



# just for testing
if __name__ == '__main__':
    language = An2()
    print language.sample_data(100)
