import itertools
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar



class ABnen(FormalLanguage):
    #((AB)^n)^n
    
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'ab%s', ['S'], 1.0)
        self.grammar.add_rule('S', 'ab', None, 2.0)

    def terminals(self):
        return list('ab')

    def sample_string(self):
         s = str(self.grammar.generate())
         return s*(len(s)/2)

    def all_strings(self):
        raise NotImplementedError
    
if __name__ == '__main__':
    language = ABnen()
    print language.sample_data(10000)

    #print list(itertools.islice(language.all_strings(),100))
