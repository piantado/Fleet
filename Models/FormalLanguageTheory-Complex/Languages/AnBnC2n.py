from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBnC2n(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sb', ['S'], 2.0)
        self.grammar.add_rule('S', 'ab',    None, 1.0)

    def terminals(self):
        return list('abc')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + 'c'*(len(s))

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n + 'b'*n + 'c'*2*n
            n += 1

# just for testing
if __name__ == '__main__':
    language = AnBnC2n()
    print language.sample_data(10000)
