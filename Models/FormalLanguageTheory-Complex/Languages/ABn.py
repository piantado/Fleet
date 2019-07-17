
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class ABn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'ab%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'ab', None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        n=1
        while True:
            yield 'ab'*n
            n += 1


if __name__ == '__main__':
    language = ABn()
    print language.sample_data(10000)
