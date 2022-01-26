from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnB2nC3n(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def terminals(self):
        return list('abc')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + 'b'*(2*len(s)) + 'c'*(3*len(s))


# just for testing
if __name__ == '__main__':
    language = AnB2nC3n()
    print language.sample_data(10000)
