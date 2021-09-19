from LOTlib.DataAndObjects import FunctionData
from LOTlib.Parsing import parseScheme,list2FunctionNode

def treebank2FunctionData(strs):
    """
        Parse treebank-style trees into FunctionNodes and return a list of FunctionData with the right format

        The data is in the following format:

            di.args = T
            di.output = []

        where we the data function "output" is implicit in T (depending on where we choose pronouns)
        SO: All the fanciness is handled in the likelihood
    """
    return map(lambda s: FunctionData(input=[list2FunctionNode(parseScheme(s))], output=None), strs)


# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# The data for the model
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

"""
    Data must be of the form where the co-reference is annotated on the Syntactic category, NOT the terminal (which is a leaf, not a FunctionNode)

    John.1 said that he.1 was happy.

"""

data = treebank2FunctionData(["(S (NP.1 Jim) (VP (V saw) (NP.2 Lyndon)))", \
          "(S (NP.1 Jim) (VP (V saw) (NP.2 he/him)))", \
          "(S (NP.1 Jim) (VP (V saw) (NP.1 himself)))", \
          "(S (NP.1 he/him) (VP (V saw) (NP.2 Lyndon)))", \

          # Behavior in embedded clauses
          "(S (NP.1 Frank) (VP (V believed)  (CC that) (S (NP.2 Joe) (VP (V tickled) (NP.2 himself)))))", \
          "(S (NP.1 Frank) (VP (V believed)  (CC that) (S (NP.2 Joe) (VP (V tickled) (NP.1 he/him)))))", \
          "(S (NP.1 Frank) (VP (V believed)  (CC that) (S (NP.2 Joe) (VP (V tickled) (NP.3 he/him)))))", \
          "(S (NP.1 he/him) (VP (V believed) (CC that) (S (NP.2 Joe) (VP (V tickled) (NP.3 Lyndon)))))", \

          # Need not be dominated by an NP
          "(S (NP.1 Jim) (VP (V saw) (NP.2 (NP.3 Frank) (CONJ and) (NP.1 himself))))", \
          "(S (NP.1 Jim) (VP (V saw) (NP.2 (NP.3 Frank) (CONJ and) (NP.4 he/him))))", \
          "(S (NP.1 Jim) (VP (V found) (NP.2 (DET a) (PP (N picture) of (NP.1 himself)))))", \
          "(S (NP.1 Jim) (VP (V found) (NP.2 (DET a) (PP (N picture) of (NP.3 he/him)))))", \
          "(S (NP.1 he/him) (VP (V saw) (NP.2 (NP.3 Frank) (CONJ and) (NP.4 Lyndon))))", \

          # How to interact with possessives / structured NPs
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (NP.1 himself)))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (NP.2 he/him)))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (NP.4 he/him)))", \
          "(S (NP.1 (NP.2 he/him) (POS -s) (NP.3 father)) (VP (V believed) (NP.4 Lyndon)))", \

          ## Bounding nodes -- sentences
          "(S (S (NP.1 Jim) (VP laughed)) (CC and) (S (NP.2 Larry) (VP (V met) (NP.2 himself))))", \
          "(S (S (NP.1 Jim) (VP laughed)) (CC and) (S (NP.2 Larry) (VP (V met) (NP.1 he/him))))", \
          "(S (S (NP.1 Jim) (VP laughed)) (CC and) (S (NP.2 Larry) (VP (V met) (NP.3 he/him))))", \
          "(S (S (NP.1 he/him) (VP laughed)) (CC and) (S (NP.2 Larry) (VP (V met) (NP.3 Lyndon))))", \

          # can have an NP as a parent
          #"(S (NP.1 Jim) (VP (V saw) (NP (NP.2 Bill) and (NP.3 he/him))))", \
          #"(S (NP (NP.1 Bill) and (NP.2 he/him)) (VP (V saw) (NP.3 Jim)))", \

          # Some examples in more complex syntax:
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.4 Frank) (VP (V tickled) (NP.1 he/him)))))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.4 Frank) (VP (V tickled) (NP.2 he/him)))))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.4 Frank) (VP (V tickled) (NP.5 he/him)))))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.4 Frank) (VP (V tickled) (NP.4 himself)))))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.2 he/him) (VP (V tickled) (NP.4 Lyndon)))))", \
          "(S (NP.1 (NP.2 Joe) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.1 he/him) (VP (V tickled) (NP.4 Lyndon)))))", \

          "(S (NP.1 (NP.2 he/him) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.2 Frank) (VP (V tickled) (NP.4 Lyndon)))))", \
          "(S (NP.1 (NP.2 he/him) (POS -s) (NP.3 father)) (VP (V believed) (CC that) (S (NP.4 Frank) (VP (V tickled) (NP.5 Lyndon)))))",

         ## Not sure what to do with this guy:
          #"(S (NP.1 he/him) (VP (V found) (NP.2 (DET a) (PP (N picture) of (NP.1 himself)))))", \

                  ]) # USED TO MULTIPLY DATA BEFORE, BUT NOW WE SET LL_WEIGHT
                   ##"(S (NP.1 Jim) (VP (V found) (NP.2 (N.3 Sam) (POS -s) (PP (N picture) of (NP.1 he/him)))))", \ ## HMM How do you get this one?

def make_data(*args):
    return data

