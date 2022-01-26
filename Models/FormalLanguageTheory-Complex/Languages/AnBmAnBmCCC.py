import itertools
from LOTlib.Miscellaneous import partitions
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBmAnBm(FormalLanguage):
    def __init__(self):
        self.grammarA = Grammar(start='A')
        self.grammarA.add_rule('A', 'a%s', ['A'], 2.0)
        self.grammarA.add_rule('A', 'a',   None, 1.0)

        self.grammarB = Grammar(start='B')
        self.grammarB.add_rule('B', 'b%s', ['B'], 2.0)
        self.grammarB.add_rule('B', 'b',   None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self):
		a = str(self.grammarA.generate())
		b = str(self.grammarB.generate())
		return a+b+a+b
		
    def all_strings(self):
        raise NotImplementedError
    

class AnBmAnBmCCC(FormalLanguage):
    def __init__(self):
        self.grammarA = Grammar(start='A')
        self.grammarA.add_rule('A', 'a%s', ['A'], 2.0)
        self.grammarA.add_rule('A', 'a',   None, 1.0)

        self.grammarB = Grammar(start='B')
        self.grammarB.add_rule('B', 'b%s', ['B'], 2.0)
        self.grammarB.add_rule('B', 'b',   None, 1.0)

    def terminals(self):
        return list('abc')

    def sample_string(self):
		a = str(self.grammarA.generate())
		b = str(self.grammarB.generate())
		return a+b+a+b+'ccc'
		
    def all_strings(self):
        raise NotImplementedError
    

# just for testing
if __name__ == '__main__':
    language = AnBmAnBmCCC()
    print language.sample_data(10000)
