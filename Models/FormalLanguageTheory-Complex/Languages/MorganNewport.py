
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class MorganNewport(FormalLanguage):
    """
    From Morgan & Newport 1981, also studied in Saffran 2001, JML
    Here, we are not doing the word learning/categorization part, just assuming that the
    parts of speech are known. Note Morgan & Newport give both a CFG and a FSM, and the language
    itself is finite (18 possible strings)

    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', '%s%s', ['AP', 'BP'], 1.0)
        self.grammar.add_rule('S', '%s%s%s', ['AP', 'BP', 'CP'], 1.0)
        self.grammar.add_rule('AP', 'A',   None, 1.0)
        self.grammar.add_rule('AP', 'AD',  None, 1.0) # two terminals, A,D

        self.grammar.add_rule('BP', 'E', None, 1.0)
        self.grammar.add_rule('BP', '%sF', ['CP'], 1.0)

        self.grammar.add_rule('CP', 'C', None, 1.0)
        self.grammar.add_rule('CP', 'CD', None, 1.0)


    def terminals(self):
        return list('ADECF')

    def all_strings(self):
        for g in self.grammar.enumerate():
            yield str(g)

class MorganMeierNewport(FormalLanguage):
    """
    From Morgan, Meier, & Newport with function word cues to phrase structure boundaries

    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', '%s%s', ['AP', 'BP'], 1.0)
        self.grammar.add_rule('S', '%s%s%s', ['AP', 'BP', 'CP'], 1.0)
        self.grammar.add_rule('AP', 'oA',   None, 1.0)
        self.grammar.add_rule('AP', 'oAD',  None, 1.0) # two terminals, A,D

        self.grammar.add_rule('BP', 'uE', None, 1.0)
        self.grammar.add_rule('BP', 'a%sF', ['CP'], 1.0)

        self.grammar.add_rule('CP', 'iC', None, 1.0)
        self.grammar.add_rule('CP', 'iCD', None, 1.0)


    def terminals(self):
        return list('ADECFouai')

    def all_strings(self):
        for g in self.grammar.enumerate():
            yield str(g)



# just for testing
if __name__ == '__main__':
    language = MorganNewport()
    print language.sample_data(10000)

    print MorganMeierNewport().sample_data(1000)

