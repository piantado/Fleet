
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class An(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def terminals(self):
        return list('a')

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n
            n += 1


# just for testing
if __name__ == '__main__':
    language = An()
    print language.sample_data(10000)
