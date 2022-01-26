from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar
from random import random 

class AnUAnBn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        
        if random() < 0.5:
            return s
        else: 
            return s + "b"*len(s)


# just for testing
if __name__ == '__main__':
    language = AnUAnBn()
    print language.sample_data(10000)
