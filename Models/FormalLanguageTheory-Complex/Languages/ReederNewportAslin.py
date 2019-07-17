
from FormalLanguage import FormalLanguage
from LOTlib.Miscellaneous import weighted_sample
import itertools

class ReederNewportAslin(FormalLanguage):
    """
    From Reeder, Newport, & Aslin 2013 - QAXBR grammar, looking to see if you generalize
    to held-out strings

    q Q = Q words
    a A s = A words
    x X c = X words
    b B n = B words
    r R = R words

    Strings are from Table 2, starred

    """
    def __init__(self):
        strings = ['axb', 'axn', 'AxB', 'Axn', 'sxb', 'sxB', 'aXB', 'aXn', 'AXb', 'AXB', 'sXb', 'sXn', 'acb', 'acB', 'Acb', 'Acn', 'scB', 'scn']
        self.test_strings = ['axB', 'Axb', 'sxn', 'aXb', 'AXn', 'sXB','acn', 'AcB', 'scb']
        assert len(set(strings).intersection(set(self.test_strings))) == 0

        self.strings = []
        for q,r in itertools.product(['', 'q', 'Q'], ['', 'r', 'R']):
            for s in strings:
                self.strings.append( q+s+r )

    def terminals(self):
        return list('aAsbBnxXcqQrR')

    def sample_string(self):
        return weighted_sample(self.strings, probs=lambda s: pow(2.0, -len(s))) # sample inversely with length, ok?

    def all_strings(self):
        for s in self.strings:
            yield s



# just for testing
if __name__ == '__main__':
    language = ReederNewportAslin()
    print language.sample_data(10000)

