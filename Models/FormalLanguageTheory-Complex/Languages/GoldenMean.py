
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class GoldenMean(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['A'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('A', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)
        self.grammar.add_rule('S', 'b',    None, 1.0)
        self.grammar.add_rule('A', 'b',    None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        raise NotImplementedError
        #n=1
        #while True:
            #yield 'a'*n
            #n += 1


# just for testing
if __name__ == '__main__':
    language = GoldenMean()
    print language.sample_data(10000)
