from LOTlib.Grammar import Grammar
from FormalLanguage import FormalLanguage


class ABnABAn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'ab%saba', ['S'], 2.0)
        self.grammar.add_rule('S', 'ababa',    None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        n=1
        while True:
            yield 'ab'*n + 'aba'*(n)
            n += 1

# just for testing
if __name__ == '__main__':
    language = ABnABAn()
    print language.sample_data(10000)
