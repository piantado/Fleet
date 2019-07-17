import itertools
from FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class  XXX(FormalLanguage):
    """
        An xxx language -- indexed but not mildly context sensitive (see https://en.wikipedia.org/wiki/Indexed_language)
    """

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',   None, 1.0)
        self.grammar.add_rule('S', 'b',   None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate()) # from (a+b)+
        return s+s+s # xxx language


    def all_strings(self):
        for l in itertools.count(1):
            for s in compute_all_strings(l, alphabet=self.terminals()):
                yield s + s + s

# just for testing
if __name__ == '__main__':
    language = XXX()
    print language.sample_data(10000)
