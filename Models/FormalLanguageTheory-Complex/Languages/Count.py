import itertools
from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar

class Count(FormalLanguage):
    """

    The language ababbabbbabbbb etc

    """

    def __init__(self):
        # This grammar is just a proxy, it gets replaced in sample
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 1.5) # if we make this 2, then we end up with things that are waay too long
        self.grammar.add_rule('S', 'a',    None, 1.0)

    def sample_string(self):
        proxy = str(self.grammar.generate())
        out = ''
        for i in range(len(proxy)):
            out = out+'a' + 'b'*(i+1)
        return out

    def terminals(self):
        return list('ab')

    def all_strings(self):
        for n in itertools.count(0):
            out = ''
            for i in range(n):
                out = out + 'a' + 'b' * (i + 1)
            yield out

# just for testing
if __name__ == '__main__':
    language = Count()
    print language.sample_data(10000)

    for i, s in enumerate(language.all_strings()):
        print s
        if i> 10: break
