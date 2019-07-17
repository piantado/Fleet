
from FormalLanguage import FormalLanguage
from random import random

class A2en(FormalLanguage):
    # A^(2^n)

    def __init__(self):
        self.grammar = None
        
    def terminals(self):
        return list('a')
    
    def sample_string(self):
        
        n=1
        while random() < (0.1): # use a different rate here so we don't get crazy things
            n += 1
            
        return 'a'*(2**n)

    def all_strings(self):
        n=1
        while True:
            yield 'a'*(2**n)

            n += 1


# just for testing
if __name__ == '__main__':
    language = A2en()
    print language.sample_data(100)
