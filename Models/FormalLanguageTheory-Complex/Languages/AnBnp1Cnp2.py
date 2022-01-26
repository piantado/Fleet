from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBnp1Cnp2(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def terminals(self):
        return list('abc')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + 'b'*(len(s)+1) + 'c'*(len(s)+2)


# just for testing
if __name__ == '__main__':
    language = AnBnp1Cnp2()
    print language.sample_data(10000)
