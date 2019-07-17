import itertools
from FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class  XX(FormalLanguage):
    """
        An xx language (for discussion see Gazdar & Pullum 1982)
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
        return s+s # xx language


    def all_strings(self):
        for l in itertools.count(1):
            for s in compute_all_strings(l, alphabet=self.terminals()):
                yield s + s

# just for testing
if __name__ == '__main__':
    language = XX()
    print language.sample_data(10000)
