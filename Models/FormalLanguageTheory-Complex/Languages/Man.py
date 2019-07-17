
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage
from random import sample

class Man(FormalLanguage):
    """
        Mam language -- all allowed variants of "m", "a", "n", uniform distribution
    """

    def __init__(self):
        self.grammar = None
        self.strings = set( ["a", "am", "mam", "an", "man" ])

    def terminals(self):
        return list('man')

    def sample_string(self): # fix that this is not CF
        return sample(self.strings, 1)[0]

    def all_strings(self):
        for m in self.strings:
            yield m

if __name__ == '__main__':
    language = Man()
    print language.sample_data(10000)