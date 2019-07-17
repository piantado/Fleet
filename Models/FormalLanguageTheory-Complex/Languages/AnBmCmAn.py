import itertools
from LOTlib.Miscellaneous import partitions
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBmCmAn(FormalLanguage):
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sa', ['S'], 2.0)
        self.grammar.add_rule('S', 'a%sa', ['T'], 1.0)
        self.grammar.add_rule('T', 'b%sc', ['T'], 2.0)
        self.grammar.add_rule('T', 'bc',    None, 1.0)

    def terminals(self):
        return list('abcd')

    def sample_string(self):
        return str(self.grammar.generate())

    def all_strings(self):
        for r in itertools.count(1):
            for n,m in partitions(r, 2, 1): # partition into two groups (NOTE: does not return both orders)
                yield 'a'*n + 'b'*m + 'c'*m + 'a'*n
                if n != m:
                    yield 'a'*m + 'b'*n + 'c'*n + 'a'*m

# just for testing
if __name__ == '__main__':
    language = AnBmCmAn()
    print language.sample_data(10000)
