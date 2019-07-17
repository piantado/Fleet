import itertools
from FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class XY(FormalLanguage):
    """
    An XY language discussed in Pullum & Gazdar, originally from Chomsky 1963 pg 378-9
    This is the set of all strings xy where x!=y. Note this is CF, contrary to Chomsky
    Here for simplicity we will just use {a,b} as the alphabet
    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',   None, 1.0)
        self.grammar.add_rule('S', 'b',   None, 1.0)
        

    def terminals(self):
        return list('ab')

    def sample_string(self):
        while True:
            x = str(self.grammar.generate())
            y = str(self.grammar.generate())
            if x != y:
                return x+y

    def all_strings(self):
        for l in itertools.count(1):
            for x in compute_all_strings(l, alphabet=self.terminals()):
                for y in compute_all_strings(l, alphabet=self.terminals()):
                    if x != y:
                        yield x+y



# just for testing
if __name__ == '__main__':
    language = XY()
    print language.sample_data(10000)
