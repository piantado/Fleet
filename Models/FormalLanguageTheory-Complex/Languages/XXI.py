import itertools
from FormalLanguage import FormalLanguage, compute_all_strings
from LOTlib.Grammar import Grammar
import re

class XXI(FormalLanguage):
    """
    A string x, then x "inverted" where as and bs are swapped
    """
    def __init__(self):
        self.grammar = Grammar(start='S')
        self.grammar.add_rule('S', 'a%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'b%s', ['S'], 2.0)
        self.grammar.add_rule('S', 'a',   None, 1.0)
        self.grammar.add_rule('S', 'b',   None, 1.0)


    def terminals(self):
        return list('ab')

    def sample_string(self):
        while True:
            x = str(self.grammar.generate())
            if len(x)==0 : continue
            v = re.sub(r"a","t", x)
            v = re.sub(r"b","a", v)
            v = re.sub(r"t","b", v)
            return x+v
        

    def all_strings(self):
        for l in itertools.count(1):
            for x in compute_all_strings(l, alphabet=self.terminals()):
                v = re.sub(r"a","t", x)
                v = re.sub(r"b","a", v)
                v = re.sub(r"t","b", v)
                yield x+v
                    



# just for testing
if __name__ == '__main__':
    language = XXI()
    print language.sample_data(10000)
