import itertools
from LOTlib.Miscellaneous import partitions
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar
from random import random

class AnBk(FormalLanguage):
    """
    Anything in A*b*
    """

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a%s', ['T'], 1.0)
        self.grammar.add_rule('T', 'b%s',  ['T'], 2.0)
        self.grammar.add_rule('T', 'b',    None, 1.0)

    def terminals(self):
        return list('ab')


# just for testing
if __name__ == '__main__':
    language = AnBk()
    print language.sample_data(10000)

    print list(itertools.islice(language.all_strings(),100))
