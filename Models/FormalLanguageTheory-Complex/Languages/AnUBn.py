
import re
from random import random 
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnUBn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self): # fix that this is not CF

        s = str(self.grammar.generate())
        
        if random() < 0.5:
            s = re.sub(r"a", "b", s)
            
        return s
        
# just for testing
if __name__ == '__main__':
    language = AnUBn()
    print language.sample_data(10000)
