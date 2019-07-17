import itertools
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar

class ABA(FormalLanguage):
    """
    Similar to Marcus ABB experiment, except we allow AAA (for simplicity)
    """

    def __init__(self):
        self.grammar = Grammar(start='S') # NOTE: This grammar does not capture the rule -- we do that in sample!
        self.grammar.add_rule('S', '%s%s', ['T','T'], 1.0)

        for t in self.terminals():
            self.grammar.add_rule('T', t, None, 1.0)

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + s[0] # copy the first  element

    def terminals(self):
        return list('gGtTnNlL') # ga gi ta ti na ni la li

    def all_strings(self):
        for t1 in self.terminals():
            for t2 in self.terminals():
                yield t1 + t2 + t1

class ABB(FormalLanguage):
    """
    Similar to Marcus ABB experiment, , except we allow AAA (for simplicity)
    """

    def __init__(self):
        self.grammar = Grammar(start='S') # NOTE: This grammar does not capture the rule -- we do that in sample!
        self.grammar.add_rule('S', '%s%s', ['T','T'], 1.0)

        for t in self.terminals():
            self.grammar.add_rule('T', t, None, 1.0)

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + s[1] # copy the second element

    def terminals(self):
        return list('gGtTnNlL') # ga gi ta ti na ni la li

    def all_strings(self):
        for t1 in self.terminals():
            for t2 in self.terminals():
                yield t1 + t2 + t2


if __name__ == '__main__':
    print ABA().sample_data(100)
    print ABB().sample_data(100)

