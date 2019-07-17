
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

# Every language uses these as X
OTHER_TERMINALS='1234567890wxyz'

class Gomez(FormalLanguage):
    """
        Gomez (2002) language 1b
    """

    def __init__(self, X):
        assert X < len(OTHER_TERMINALS)
        self.X=X

        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sd', ['X'], 1.0)
        self.grammar.add_rule('S', 'b%se', ['X'], 1.0)

        for x in OTHER_TERMINALS[:self.X]:
            self.grammar.add_rule('X', '%s'%x, None, 1.0)

    def terminals(self):
        return list('abde'+OTHER_TERMINALS[:self.X] )

    def all_strings(self):
        for g in self.grammar.enumerate():
            yield str(g)


class Gomez2(Gomez):
    def __init__(self):
        Gomez.__init__(self, X=2)

class Gomez6(Gomez):
    def __init__(self):
        Gomez.__init__(self, X=6)

class Gomez12(Gomez):
    def __init__(self):
        Gomez.__init__(self, X=12)

# just for testing
if __name__ == '__main__':
    language = Gomez2()
    # print language.sample_data(10000)
    print list(language.all_strings())