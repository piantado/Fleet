""" A bunch of languages from Pullum 2006 """
import numpy 
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class PullumR(FormalLanguage):

    def __init__(self):
        pass


    def terminals(self):
        return list('abcd')

    def sample_string(self):
        x = numpy.random.geometric(p=.5)-1 # might be zero
        y = numpy.random.geometric(p=.5)-1
        z = numpy.random.geometric(p=.5)-1
        w = numpy.random.geometric(p=.5)-1
        
        return 'a' + ('c'*x + 'd' + 'c'*y + 'd' +'c'*z)*w + 'b'

    def all_strings(self):
        raise NotImplementedError
    
    
class ApBAp(FormalLanguage):
    """$a^+ba^+$"""
    
    def __init__(self):
        pass

    def terminals(self):
        return list('ab')

    def sample_string(self):
        x = numpy.random.geometric(p=.5) # +
        y = numpy.random.geometric(p=.5)
        
        return 'a'*x + 'b' + 'a'*y

    def all_strings(self):
        raise NotImplementedError

class AsBAsp(FormalLanguage):
    """ $(a^*)(ba^*)^+$  """
    
    def __init__(self):
        pass

    def terminals(self):
        return list('abcd')

    def sample_string(self):
        x = numpy.random.geometric(p=.5)-1
        y = numpy.random.geometric(p=.5)-1
        z = numpy.random.geometric(p=.5)
        
        return 'a'*x + ('b' + 'a'*y)*z

    def all_strings(self):
        raise NotImplementedError

class ApBApp(FormalLanguage):
    """ $(a^*)(ba^+)^+$  """
    
    def __init__(self):
        pass

    def terminals(self):
        return list('abcd')

    def sample_string(self):
        x = numpy.random.geometric(p=.5)
        y = numpy.random.geometric(p=.5)
        z = numpy.random.geometric(p=.5)
        
        return 'a'*x + ('b' + 'a'*y)*z

    def all_strings(self):
        raise NotImplementedError

from AB import AB

class CountA2(FormalLanguage):
    """  $\lbrace w \in \lbrace a,b \rbrace^+ : count(a,w) \geq 2 \brace$  """
    
    def __init__(self):
        self.L = AB()

    def terminals(self):
        return list('ab')

    def sample_string(self):
        while True:
            s = self.L.sample_string()
            if(s.count('a') >= 2): return s

    def all_strings(self):
        raise NotImplementedError



class CountAEven(FormalLanguage):
    """ $\lbrace w \in \lbrace a,b \rbrace^+ : count(a,w) \text{ is even} \brace$ """
    
    def __init__(self):
        self.L = AB()

    def terminals(self):
        return list('ab')

    def sample_string(self):
        while True:
            s = self.L.sample_string()
            if(s.count('a')%2==0):
                return s

    def all_strings(self):
        raise NotImplementedError

# just for testing
if __name__ == '__main__':
    language = ApBApp()
    print language.sample_data(10000)
