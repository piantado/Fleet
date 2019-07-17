import numpy as np
from collections import Counter
from LOTlib.DataAndObjects import FunctionData
from LOTlib.Miscellaneous import logsumexp, Infinity, weighted_sample
from copy import deepcopy

def compute_all_strings(l, alphabet='ab'):
    """ yield all strings of length l on a given alphabet  """
    if l == 1:
        for a in alphabet:
            yield a
    else:
        for r in compute_all_strings(l-1):
            for a in alphabet:
                yield r+a


class FormalLanguage(object):
    """
    Set up a super-class for formal languages, so we can compute things like accuracy, precision, etc.
    """
    ALPHA = 0.99

    def sample_string(self):
        return str(self.grammar.generate())

    def sample_data(self, n):
        # Sample a string of data
        cnt = Counter()
        for _ in xrange(n):
            cnt[self.sample_string()] += 1

        return [FunctionData(input=[], output=cnt, alpha=self.ALPHA)]

    def terminals(self):
        """ This returns a list of terminal symbols, specific to each language
        """
        raise NotImplementedError

    def all_strings(self):
        """ Yield all strings in the language, hopefully in order of increasing length
        """
        raise NotImplementedError
