from FormalLanguage import FormalLanguage
from LOTlib.Grammar import Grammar
from pickle import load

class Braine66(FormalLanguage):
    """
    Language from Braine '66
    """

    def __init__(self):
        self.grammar = Grammar(start='S')
        
        self.grammar.add_rule('S', '%s', ['A'], 6.0)
        self.grammar.add_rule('S', '%s', ['PQ'], 3.0)
        self.grammar.add_rule('S', '%s%s', ['PQ', 'A'], 2.0)
        self.grammar.add_rule('S', '%s%s', ['A', 'PQ'], 1.0)
        # A phrases:
        # ob = b
        # ordem = d
        # remin = r
        # gice = g
        # kivil = k
        # noot = n
        # yarmo = f

        # PQ Phrases:
        # ged = G
        # mervo = m
        # yag = y
        # leck = l
        
        # som = s
        # eena = e
        # wimp = w
        
        self.grammar.add_rule('A', 'fbd', None, 1.0) # yarmo ob ordem
        self.grammar.add_rule('A', 'frg', None, 1.0)
        self.grammar.add_rule('A', 'fk', None, 1.0)
        self.grammar.add_rule('A', 'fn', None, 1.0)
        
        self.grammar.add_rule('PQ', 'Gms', None, 1.0)
        self.grammar.add_rule('PQ', 'Gys', None, 1.0)
        self.grammar.add_rule('PQ', 'Gls', None, 1.0)
        
        self.grammar.add_rule('PQ', 'Gme', None, 1.0)
        self.grammar.add_rule('PQ', 'Gye', None, 1.0)
        self.grammar.add_rule('PQ', 'Gle', None, 1.0)
        
        self.grammar.add_rule('PQ', 'Gmw', None, 1.0)
        self.grammar.add_rule('PQ', 'Gyw', None, 1.0)
        self.grammar.add_rule('PQ', 'Glw', None, 1.0)
        

    def terminals(self):
        return list('bdrgknfGmylsew')

    def all_strings(self):
        for g in self.grammar.enumerate():
            yield str(g)

# just for testing
if __name__ == '__main__':
    language = Braine66()
    data = language.sample_data(1000)
    
    #print data
    
    for r in data[0].output.items():
        print "{\"%s\",%i}\n" % r,
