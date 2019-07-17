import itertools
from LOTlib.Miscellaneous import partitions
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBmCnDm(FormalLanguage):
    def __init__(self):
        self.grammarA = Grammar(start='A')
        self.grammarA.add_rule('A', 'a%s', ['A'], 2.0)
        self.grammarA.add_rule('A', 'a',   None, 1.0)

        self.grammarB = Grammar(start='B')
        self.grammarB.add_rule('B', 'b%s', ['B'], 2.0)
        self.grammarB.add_rule('B', 'b',   None, 1.0)

    def terminals(self):
        return list('abcd')

    def sample_string(self):
		a = str(self.grammarA.generate())
		b = str(self.grammarB.generate())
		return a+b+('c'*len(a))+('d'*len(b))
		
    def all_strings(self):
        for r in itertools.count(1):
            for n,m in partitions(r, 2, 1): # partition into two groups (NOTE: does not return both orders)
                yield 'a'*n + 'b'*m + 'c'*n + 'a'*m
                if n != m:
                    yield 'a'*m + 'b'*n + 'c'*m + 'a'*n

# just for testing
if __name__ == '__main__':
    language = AnBmCnDm()
    print language.sample_data(10000)
