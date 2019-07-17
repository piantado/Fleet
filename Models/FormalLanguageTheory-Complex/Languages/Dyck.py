import itertools
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

def dyck_at_depth(n):
    if n == 1:
        yield '()'
    else:
        for k in dyck_at_depth(n-1):
            yield '()' + k
            yield '('+k+')'

class Dyck(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', '(%s)', ['S'], 1.0)
        self.grammar.add_rule('S', '()%s', ['S'], 1.0)
        self.grammar.add_rule('S', '()',    None, 1.0)

    def terminals(self):
        return list(')(')

    def all_strings(self):
        for n in itertools.count(1):
            for s in dyck_at_depth(n):
                yield s



# just for testing
if __name__ == '__main__':
    language = Dyck()
    print language.sample_data(10000)
