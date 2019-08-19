from LOTlib.Grammar import Grammar
from FormalLanguage import FormalLanguage


class AnABn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sab', ['S'], 2.0)
        self.grammar.add_rule('S', 'aab',    None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n + 'ab'*(n)
            n += 1

# just for testing
if __name__ == '__main__':
    language = AnABn()
    print language.sample_data(10000)
