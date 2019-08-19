
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage
from LOTlib.Miscellaneous import weighted_sample
import numpy 

class Bach3(FormalLanguage):
    """
       Equal numbers of a,b,c in any order
    """

    def __init__(self):
        pass 
    
    def terminals(self):
        return list('abc')

    def sample_string(self): # fix that this is not CF
        n = numpy.random.geometric(p=1./3.)
        return ''.join(numpy.random.permutation(list('abc'*n)))
    
    def all_strings(self):
        raise NotImplementedError

class Bach2(FormalLanguage):
    """
       Equal numbers of a,b,c in any order
    """

    def __init__(self):
        pass 
    
    def terminals(self):
        return list('ab')

    def sample_string(self): # fix that this is not CF
        n = numpy.random.geometric(p=1./3.)
        return ''.join(numpy.random.permutation(list('ab'*n)))
    
    def all_strings(self):
        raise NotImplementedError


if __name__ == '__main__':
    language = Bach3()
    print language.sample_data(10000)
