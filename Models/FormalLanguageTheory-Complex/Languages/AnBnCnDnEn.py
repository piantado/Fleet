from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class AnBnCnDnEn(FormalLanguage):

    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%sb', ['S'], 2.0)
        self.grammar.add_rule('S', 'ab',    None, 1.0)

    def terminals(self):
        return list('abcde')

    def sample_string(self): # fix that this is not CF
        s = str(self.grammar.generate())
        return s + 'c'*(len(s)/2) + 'd'*(len(s)/2) + 'e'*(len(s)/2)

    def all_strings(self):
        n=1
        while True:
            yield 'a'*n + 'b'*n + 'c'*n + 'd'*n + 'e'*n
            n += 1

# just for testing
if __name__ == '__main__':
    language = AnBnCnDnEn()
    print language.sample_data(10000)
