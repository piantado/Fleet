
from LOTlib.Projects.FormalLanguageTheory.Language.FormalLanguage import FormalLanguage
from random import random

fib_cache = dict()
def fib(n):
    v = None
    if n not in fib_cache:
        if n <= 1:
            v = 1
        else:
            v = fib(n-1)+fib(n-2)
        fib_cache[n] = v
        return v
    else:
        return fib_cache[n]

class Fibo(FormalLanguage):
    """
    a^n : n is a fibonacci number
    """

    def __init__(self):
        self.grammar = None

    def terminals(self):
        return list('a')

    def sample_string(self): # fix that this is not CF
        n=0
        while random() < (1.0/2.0):
            n += 1
        return 'a'*fib(n)

        # just for testing
    def all_strings(self): # fix that this is not CF
        n=0
        while True:
            yield 'a'*fib(n)
            n += 1


if __name__ == '__main__':
    language = Fibo()
    print language.sample_data(10000)
