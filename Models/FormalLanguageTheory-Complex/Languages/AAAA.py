
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage
from LOTlib.Miscellaneous import weighted_sample

class AAAA(FormalLanguage):
    """
       The language {a,aa,aaa,aaaa}
    """

    def __init__(self):
        self.grammar = None
        self.strings = ["a", "aa", "aaa", "aaaa" ]
        self.probs   = [ 2**-len(x) for x in self.strings] # geometric distribution

    def terminals(self):
        return list('a')

    def sample_string(self): # fix that this is not CF
        return weighted_sample(self.strings, probs=self.probs)

    def all_strings(self):
        for m in self.strings:
            yield m


class AAA(FormalLanguage):

    def __init__(self):
        self.grammar = None
        self.strings = ["a", "aa", "aaa" ]
        self.probs   = [ 2**-len(x) for x in self.strings] # geometric distribution

    def terminals(self):
        return list('man')

    def sample_string(self): # fix that this is not CF
        return weighted_sample(self.strings, probs=self.probs)

    def all_strings(self):
        for m in self.strings:
            yield m


if __name__ == '__main__':
    language = AAAA()
    print language.sample_data(10000)
