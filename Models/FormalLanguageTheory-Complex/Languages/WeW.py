import itertools
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar



class WeW(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a', None, 1.0)
        self.grammar.add_rule('S', 'b', None, 1.0)

    def terminals(self):
        return list('ab')

    def sample_string(self):
        w = str(self.grammar.generate())
        return w*len(w)

if __name__ == '__main__':
    language = WeW()
    print language.sample_data(10000)

    print list(itertools.islice(language.all_strings(),100))
