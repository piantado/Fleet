
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sb', ['S'], 2.0)
        self.grammar.add_rule('S', 'ab',    None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n + 'b'*n
            n += 1


# just for testing
if __name__ == '__main__':
    language = AnBn()
    print language.sample_data(10000)
