#pragma once 

#include "Grammar.h"
#include "Singleton.h"

#include "DSL.h"


// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<Utterance,TruthValue,    TruthValue,bool,Utterance,Set,MyObject,MyColor,MyShape>,
				  public Singleton<MyGrammar> {
					  
	using Super =        Grammar<Utterance,TruthValue,    TruthValue,bool,Utterance,Set,MyObject,MyColor,MyShape>;
	using Super::Super;
public:

	MyGrammar() {
		using namespace DSL; 
		
		// Set -> bool
		add("empty(%s)",     empty); 
		add("singleton(%s)", singleton); 
		add("doubleton(%s)", doubleton); 
		add("tripleton(%s)", tripleton); 
		
		// Set x Set -> bool
		add("subset(%s,%s)",  subset);
		add("card_gt(%s,%s)", card_gt);
		add("card_eq(%s,%s)", card_eq);
		add("eq(%s,%s)", eq); 
	
		// bool x bool -> TruthValue
		add("presup(%s ; %s)",  presup);
		
		// Set
		add("\u00D8",  emptyset);		
		add("%s.context", context);
		add("%s.shape", shape);
		add("%s.color", color);
		
		// Set -> Set
		add("intersection(%s,%s)", intersection);		
		add("union(%s,%s)",  myunion);
		add("complement(%s,%s)", complement);
		add("difference(%s,%s)", difference);
			
		add("true",          +[]() -> bool               { return true; });
		add("false",         +[]() -> bool               { return false; });
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
			
		add("u",              Builtins::X<MyGrammar>);	
		
//		// add recursive calls
//		for(size_t a=0;a<words.size();a++) {	
//			add("F"+str(a)+"(%s)" ,  Builtins::Recurse<MyGrammar>, 1.0, a);
//		}
	}
		
} grammar;
