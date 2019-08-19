import itertools
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar



class AB(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a', None, 1.0)
        self.grammar.add_rule('S', 'b', None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        for l in itertools.count(1):
            for s in compute_all_strings(l, alphabet='ab'):
                yield s
                
class aABb(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sb', ['T'], 2.0)
        self.grammar.add_rule('T', 'a%s', ['T'], 2.0)
        self.grammar.add_rule('T', 'b%s', ['T'], 2.0)
        self.grammar.add_rule('T', 'a', None, 1.0)
        self.grammar.add_rule('T', 'b', None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        raise NotImplementedError

class ABaaaAB(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', '%saaa%s', ['T', 'T'], 2.0)
        self.grammar.add_rule('T', 'a%s', ['T'], 2.0)
        self.grammar.add_rule('T', 'b%s', ['T'], 2.0)
        self.grammar.add_rule('T', 'a', None, 1.0)
        self.grammar.add_rule('T', 'b', None, 1.0)

    def terminals(self):
        return list('ab')

    def all_strings(self):
        raise NotImplementedError

if __name__ == '__main__':
    language = AB()
    print language.sample_data(10000)

    print list(itertools.islice(language.all_strings(),100))
