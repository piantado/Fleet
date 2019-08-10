import itertools
from FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class ABAnBn(FormalLanguage):
    """
    An AB language followed by AnBn
    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 1.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 1.0)
        self.grammar.add_rule('S', '%s', ['Q'], 2.0)
        self.grammar.add_rule('S', '%s', ['Q'], 2.0)
        
        self.grammar.add_rule('Q', 'a%sb', ['Q'], 2.0)
        self.grammar.add_rule('Q', 'ab',   None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self):
        while True:
            x = str(self.grammar.generate())
            y = str(self.grammar.generate())
            if x != y:
                return x+y

    def all_strings(self):
        assert False


# just for testing
if __name__ == '__main__':
    language = ABAnBn()
    print language.sample_data(10000)
