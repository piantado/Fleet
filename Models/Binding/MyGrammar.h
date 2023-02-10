#pragma once 

#include<vector>
#include<functional>

#include "Grammar.h"
#include "Singleton.h"

using TreeSequence = std::vector<BindingTree*>;
using BindingTreeToBindingTree = std::function<BindingTree*(BindingTree*)>;
using BindingTreeToSequence    = std::function<TreeSequence(BindingTree*)>;
using BindingTreeToBool        = std::function<bool(BindingTree*)>;
using TxTtoBool                = std::function<bool(BindingTree*,BindingTree*)>;

//std::function coreferent = +[](BindingTree* x) -> BindingTree* { 
//	if(x==nullptr) throw TreeException();
//	return x->coreferent();
//};

std::function parent = +[](BindingTree* x) -> BindingTree* { 
	if(x == nullptr) throw TreeException();
	return x->parent; 
};

std::function children = +[](BindingTree* x) -> TreeSequence {
	if(x == nullptr) return {};
	else {
		TreeSequence out;
		for(size_t i=0;i<x->nchildren();i++) {
			out.push_back(&x->child(i));
		}
		return out; 
	}		
};

std::function ancestors = +[](BindingTree* x) -> TreeSequence {
	if(x == nullptr) return {};
	else {
		TreeSequence out;
		while(x->parent != nullptr) {
			out.push_back(x->parent);
			x = x->parent; 
		}
		return out; 
	}		
};

std::function is_subject = +[](BindingTree* x) -> bool {
	return (x != nullptr) and (x->pos==POS::NPS); 
};

std::function has_index = +[](BindingTree* x) -> bool {
	return (x != nullptr) and (x->referent != -1); 
};

std::function null = +[](BindingTree* x) -> bool {
	return x == nullptr;
};

std::function eq_tree  = +[](BindingTree* x, BindingTree* y) -> bool { return x == y;};

std::function corefers = +[](BindingTree* x, BindingTree* y) -> bool { 
	return (x != nullptr) and (y != nullptr) and (x->referent == y->referent);
};

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*,TreeSequence,BindingTreeToBindingTree,BindingTreeToSequence,BindingTreeToBool,TxTtoBool>,
				  public Singleton<MyGrammar> {
					  
	using Super =        Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*,TreeSequence,BindingTreeToBindingTree,BindingTreeToSequence,BindingTreeToBool,TxTtoBool>;
	using Super::Super;
public:

	MyGrammar() {
		
		// equality
		add("eq_bool(%s,%s)",  +[](bool a, bool b) -> bool { return (a==b); }, 1./3.);		
		add("eq_str(%s,%s)",   +[](S a, S b) -> bool { return (a==b); },       1./3.);		
		add("eq_pos(%s,%s)",   +[](POS a, POS b) -> bool { return (a==b); },   1./3.);		
		

		// pos predicates -- part of speech (NOT position)
		add("pos(%s)",           +[](BindingTree* x) -> POS { 
			if(x==nullptr) throw TreeException();
			return x->pos;
		});		
								
		add("word(%s)",  +[](BindingTree* x) -> S { 
			if(x==nullptr) throw TreeException();
			if(x->target) throw TreeException(); // well it's very clever -- we can't allow label on the target or the problem is trivial
			return x->word;
		});
			
		add("true",          +[]() -> bool               { return true; }, 0.1);
		add("false",         +[]() -> bool               { return false; }, 0.1);
		add("and(%s,%s)",    Builtins::And<MyGrammar>, 1./3.);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>,  1./3.);
		add("not(%s)",       Builtins::Not<MyGrammar>,  1./3.);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>, 1./3.);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,POS>, 1./3.);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,BindingTree*>, 1./3.);

		add("x",              Builtins::X<MyGrammar>, TERMINAL_P);	
		
		for(auto& w : words) {
			add_terminal<S>(Q(w), w, TERMINAL_P/words.size());
		}
		
		for(auto [l,p] : posmap) {
			add_terminal<POS>(Q(l), p, TERMINAL_P/posmap.size());		
		}
		
//		add("pstr(%s)",      +[](POS x) -> POS { 
//			print(str(int(x)));
//			return x;
//		});
		
		add("applyC(%s,%s)",      +[](TxTtoBool f, BindingTree* x) -> BindingTreeToBool { 
			return [=](BindingTree* t) { return f(x,t); };
		});
		add("applyCC(%s,%s,%s)",      +[](TxTtoBool f, BindingTree* x, BindingTree* y) -> bool { 
			return f(x,y);
		});
		add("eq_tree",  +[]() -> TxTtoBool { return  eq_tree; } );
		add("corefers", +[]() -> TxTtoBool { return  corefers; } );
		
		// functions on trees
		add("applyR(%s,%s)",      +[](BindingTreeToBindingTree f, BindingTree* x) -> BindingTree* { return f(x); } );
		add("parent",     +[]() -> BindingTreeToBindingTree { return parent;}) ;
//		add("coreferent", +[]() -> BindingTreeToBindingTree { return coreferent;}) ;
		
		// trees to tree sequences
		add("applyS(%s,%s)", +[](BindingTreeToSequence s, BindingTree* x) -> TreeSequence { return s(x);});
		add("children",     +[]() -> BindingTreeToSequence { return children;});		
		add("ancestors",    +[]() -> BindingTreeToSequence { return ancestors;});
		
		// predicates on trees
		add("applyB(%s,%s)",    +[](BindingTreeToBool f, BindingTree* x) -> bool { return f(x);});
		add("has_index",    +[]() -> BindingTreeToBool { return has_index;});
		add("is_subject",    +[]() -> BindingTreeToBool { return is_subject;});
		add("null",         +[]() -> BindingTreeToBool { return null;});
		
		// operations on tree sequences
		add("empty(%s)",  +[](TreeSequence s) -> bool { 
			return s.size() == 0;
		}); 
		add("head(%s)",  +[](TreeSequence s) -> BindingTree* { 
			if(s.empty()) 
				return nullptr; // throw TreeException(); // or could except but thats trickier
			else 
				return s.at(0);
		}); 
		add("tail(%s)",  +[](TreeSequence s) -> TreeSequence { 
			if(s.empty()) 
				return {}; // throw TreeException()
			else {
				s.erase(s.begin());
				return s; 
			}
		}); 
		add("append(%s,%s)",  +[](TreeSequence x, TreeSequence y) -> TreeSequence { 
			for(auto& yi : y) {
				x.push_back(yi);
			}
			return x; 
		}); 
		add("map(%s,%s)",  +[](BindingTreeToBindingTree f, TreeSequence s) -> TreeSequence { 
			TreeSequence out;
			for(auto& x : s) {
				out.push_back(f(x));
			}
			return out; 
		}); 
		add("map_append(%s,%s)",  +[](BindingTreeToSequence f, TreeSequence s) -> TreeSequence { 
			TreeSequence out;
			for(auto& x : s) {
				for(auto& y : f(x)) {
					out.push_back(y);
				}
			}
			return out; 
		}); 
		
		add("filter(%s,%s)",  +[](BindingTreeToBool f, TreeSequence s) -> TreeSequence { 
			TreeSequence out;
			for(auto& x : s) {
				if(f(x)) out.push_back(x);
			}
			return out; 
		}); 
		
		// NOTE THIS DOES NOT WORK WITH THE CACHED VERSION
//		add("F(%s,%s)" ,  Builtins::LexiconRecurse<MyGrammar,S>); // note the number of arguments here
	}
		
} grammar;

