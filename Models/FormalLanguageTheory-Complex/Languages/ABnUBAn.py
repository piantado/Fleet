from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar
from random import random 
import re

class ABnUBAn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'x%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'x',    None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        
        if random() < 0.5:
            return re.sub(r"x", "ab", s)
        else: 
            return re.sub(r"x", "ba", s)


# just for testing
if __name__ == '__main__':
    language = ABnUBAn()
    print language.sample_data(10000)
