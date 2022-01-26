import itertools
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class Unequal(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a', None, 1.0)
        self.grammar.add_rule('S', 'b', None, 1.0)

    def sample_string(self):
        while True:
            s = str(self.grammar.generate())
            if(s.count("a") != s.count("b")):
                return s 
            
    def terminals(self):
        return list('ab')

if __name__ == '__main__':
    language = Unequal()
    print language.sample_data(10000)
