
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnCBn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sb', ['S'], 2.0)
        self.grammar.add_rule('S', 'acb',    None, 1.0)

    def terminals(self):
        return list('acb')

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n + 'c' + 'b'*n
            n += 1


# just for testing
if __name__ == '__main__':
    language = AnCBn()
    print language.sample_data(10000)
