#pragma once 

#include "Grammar.h"
#include "Singleton.h"

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*>,
				  public Singleton<MyGrammar> {
					  
	using Super =        Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*>;
	using Super::Super;
public:

	MyGrammar() {

		add("null(%s)",  +[](BindingTree* x) -> bool { 
			return x == nullptr;
		}); 
		
		add("parent(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x == nullptr) throw TreeException();
			return x->parent; 
		});
		
		add("root(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x==nullptr) throw TreeException();			
			return x->root();
		});		

		// equality
		add("eq_bool(%s,%s)",   +[](bool a, bool b) -> bool { return (a==b); });		
		add("eq_str(%s,%s)",    +[](S a, S b) -> bool { return (a==b); });		
		add("eq_pos(%s,%s)",    +[](POS a, POS b) -> bool { return (a==b); });		
		add("eq_bt(%s,%s)",     +[](BindingTree* x, BindingTree* y) -> bool { return x == y;});

		// pos predicates -- part of speech (NOT position)
		add("pos(%s)",           +[](BindingTree* x) -> POS { 
			if(x==nullptr) throw TreeException();
			return x->pos;
		});		

		// tree predicates		
		add("coreferent(%s)",  +[](BindingTree* x) -> BindingTree* { 
			if(x==nullptr) throw TreeException();
			return x->coreferent();
		});
		
		add("corefers(%s)",  +[](BindingTree* x) -> bool { 
			if(x==nullptr) throw TreeException();
			return x->coreferent() != nullptr;
		});
				
		add("leaf(%s)",  +[](BindingTree* x) -> bool { 
			if(x==nullptr) throw TreeException();
			return x->nchildren() == 0;
		});
								
		add("word(%s)",  +[](BindingTree* x) -> S { 
			if(x==nullptr) throw TreeException();
			if(x->target) throw TreeException(); // well it's very clever -- we can't allow label on the target or the problem is trivial
			return x->word;
		});
				
		add("dominates(%s,%s)",  +[](BindingTree* x, BindingTree* y) -> bool { 
			// NOTE: a node will dominate itself
			if(y == nullptr or x == nullptr) 
				throw TreeException();
							
			while(true) {
				if(y == nullptr) return false; // by definition, null doesn't dominate anything
				else if(y == x) return true;
				y = y->parent;
			}
		});
		
		add("first-dominating(%s,%s)",  +[](POS s, BindingTree* x) -> BindingTree* { 	
			if(x == nullptr) throw TreeException();
			
			while(true) {
				if(x == nullptr) return nullptr; // by definition, null doesn't dominate anything
				else if(x->pos == s) return x;
				x = x->parent;
			}
		});			
			
		add("true",          +[]() -> bool               { return true; }, TERMINAL_P);
		add("false",         +[]() -> bool               { return false; }, TERMINAL_P);
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,POS>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,BindingTree*>);

		add("x",              Builtins::X<MyGrammar>, 10);	
		
		for(auto& w : words) {
			add_terminal<S>(Q(w), w, TERMINAL_P/words.size());
		}
		
		for(auto [l,p] : posmap) {
			add_terminal<POS>(Q(l), p, TERMINAL_P/posmap.size());		
		}
		
		// NOTE THIS DOES NOT WORK WITH THE CACHED VERSION
//		add("F(%s,%s)" ,  Builtins::LexiconRecurse<MyGrammar,S>); // note the number of arguments here
	}
		
} grammar;

