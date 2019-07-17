
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class Reber(FormalLanguage):
    """
    From Reber, 1967
    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'T%s', ['S1'], 1.0)
        self.grammar.add_rule('S', 'V%s', ['S3'], 1.0)

        self.grammar.add_rule('S1', 'P%s', ['S1'], 1.0)
        self.grammar.add_rule('S1', 'T%s', ['S2'], 1.0)

        self.grammar.add_rule('S3', 'X%s', ['S3'], 1.0)
        self.grammar.add_rule('S3', 'V%s', ['S4'], 1.0)

        self.grammar.add_rule('S2', 'X%s', ['S3'], 1.0)
        self.grammar.add_rule('S2', 'S',   None, 1.0)

        self.grammar.add_rule('S4', 'P%s', ['S2'], 1.0)
        self.grammar.add_rule('S4', 'S',   None, 1.0)

    def terminals(self):
        return list('PSTVX')

    def all_strings(self):
        for g in self.grammar.enumerate():
            yield str(g)



# just for testing
if __name__ == '__main__':
    language = Reber()
    print language.sample_data(10000)

