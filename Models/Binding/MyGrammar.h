#pragma once 

#include<vector>
#include<functional>

#include "Grammar.h"
#include "Singleton.h"

using TSeq       = std::vector<BindingTree*>;
using TtoT       = std::function<BindingTree*(BindingTree*)>;
using TtoTSeq    = std::function<TSeq(BindingTree*)>;
using TtoBool    = std::function<bool(BindingTree*)>;
using TxTtoBool  = std::function<bool(BindingTree*,BindingTree*)>;

//std::function coreferent = +[](BindingTree* x) -> BindingTree* { 
//	if(x==nullptr) throw TreeException();
//	return x->coreferent();
//};

std::function parent = +[](BindingTree* x) -> BindingTree* { 
	if(x == nullptr) return nullptr;
	return x->parent; 
};

std::function children = +[](BindingTree* x) -> TSeq {
	if(x == nullptr) return {};
	else {
		TSeq out;
		for(size_t i=0;i<x->nchildren();i++) {
			out.push_back(&x->child(i));
		}
		return out; 
	}		
};

std::function ancestors = +[](BindingTree* x) -> TSeq {
	if(x == nullptr) return {};
	else {
		TSeq out;
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

std::function gt_linear  = +[](BindingTree* x, BindingTree* y) -> bool { 
	return (x != nullptr) and (y != nullptr) and (x->linear_order > y->linear_order);
};

// declare a grammar with our primitives
// Note that this ordering of primitives defines the order in Grammar
class MyGrammar : public Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*,TSeq,TtoT,TtoTSeq,TtoBool,TxTtoBool>,
				  public Singleton<MyGrammar> {
					  
	using Super =        Grammar<BindingTree*,bool,    S,POS,int,bool,BindingTree*,TSeq,TtoT,TtoTSeq,TtoBool,TxTtoBool>;
	using Super::Super;
public:

	MyGrammar() {

		// pos predicates -- part of speech (NOT position)
//		add("pos(%s)",           +[](BindingTree* x) -> POS { 
//			if(x==nullptr) throw TreeException();
//			return x->pos;
//		});		
//								
//		add("word(%s)",  +[](BindingTree* x) -> S { 
//			if(x==nullptr) throw TreeException();
//			if(x->target) throw TreeException(); // well, it's very clever -- we can't allow label on the target or the problem is trivial
//			return x->word;
//		});
			
		add("true",          +[]() -> bool               { return true; }, 0.1);
		add("false",         +[]() -> bool               { return false; }, 0.1);
		add("and(%s,%s)",    Builtins::And<MyGrammar>,    1./4.);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>,     1./4.);
		add("iff(%s,%s)",    Builtins::Iff<MyGrammar>,    1./4.);
		add("not(%s)",       Builtins::Not<MyGrammar>,    1./4.);
		
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
		
		add("applyC(%s,%s)",      +[](TxTtoBool f, BindingTree* x) -> TtoBool { 
			return [=](BindingTree* t) { return f(x,t); };
		});
		add("applyCC(%s,%s,%s)",      +[](TxTtoBool f, BindingTree* x, BindingTree* y) -> bool { 
			return f(x,y);
		});
		add("eq_tree",  +[]() -> TxTtoBool { return  eq_tree; } );
		add("corefers", +[]() -> TxTtoBool { return  corefers; } );
		
		// functions on trees
		add("applyR(%s,%s)",      +[](TtoT f, BindingTree* x) -> BindingTree* { return f(x); } );
		add("parent",             +[]() -> TtoT { return parent;}) ;
//		add("coreferent", +[]() -> TtoT { return coreferent;}) ;
		
		// trees to tree sequences
		add("applyS(%s,%s)", +[](TtoTSeq s, BindingTree* x) -> TSeq { return s(x);});
		add("children",     +[]() -> TtoTSeq { return children;});		
		add("ancestors",    +[]() -> TtoTSeq { return ancestors;});
		
		// predicates on trees
		add("applyB(%s,%s)",    +[](TtoBool f, BindingTree* x) -> bool { return f(x);});
		add("has_index",    +[]() -> TtoBool { return has_index;});
		add("is_subject",    +[]() -> TtoBool { return is_subject;});
		add("null",         +[]() -> TtoBool { return null;});
		
		// predicates can also be defined by these fancy types
		// which check equality to some specific word or POS
//		add("eq_word(%s)",   +[](S a) -> TtoBool { 
//			std::function f = [=](BindingTree* t) -> bool {
//				return (t != nullptr) and (t->word == a);
//			};
//			return f; 
//		});				
		
		add("eq_pos(%s)",   +[](POS a) -> TtoBool { 
			std::function f = [=](BindingTree* t) -> bool {
				return (t != nullptr) and (t->pos == a);
			};
			return f; 
		});		

		// Or we can extract these from trees. This has the advantage over pos(%s) and word(%s)
		// above because we can handle nullptr tree arguments gracefully. 
//		add("eq_wordT(%s)",   +[](BindingTree* a) -> TtoBool { 
//			std::function f = [=](BindingTree* t) -> bool {
//				return (t != nullptr) and (a != nullptr) and (t->word == a->word);
//			};
//			return f; 
//		});				
		
		add("eq_posT(%s)",   +[](BindingTree* a) -> TtoBool { 
			std::function f = [=](BindingTree* t) -> bool {
				return (t != nullptr) and (a != nullptr) and (t->pos == a->pos);
			};
			return f; 
		});		
		
		// operations on tree sequences
		add("empty(%s)",  +[](TSeq s) -> bool { 
			return s.size() == 0;
		}); 
		add("head(%s)",  +[](TSeq s) -> BindingTree* { 
			if(s.empty()) 
				return nullptr; // throw TreeException(); // or could except but thats trickier
			else 
				return s.at(0);
		}); 
		add("tail(%s)",  +[](TSeq s) -> TSeq { 
			if(s.empty()) 
				return {}; // throw TreeException()
			else {
				s.erase(s.begin());
				return s; 
			}
		}); 
		add("append(%s,%s)",  +[](TSeq x, TSeq y) -> TSeq { 
			for(auto& yi : y) {
				x.push_back(yi);
			}
			return x; 
		}); 
		add("reverse(%s)",  +[](TSeq s) -> TSeq { 
			std::reverse(s.begin(), s.end());
			return s;
		}); 
		add("map(%s,%s)",  +[](TtoT f, TSeq s) -> TSeq { 
			TSeq out;
			for(auto& x : s) {
				out.push_back(f(x));
			}
			return out; 
		}); 
		add("map_append(%s,%s)",  +[](TtoTSeq f, TSeq s) -> TSeq { 
			TSeq out;
			for(auto& x : s) {
				for(auto& y : f(x)) {
					out.push_back(y);
				}
			}
			return out; 
		}); 
		
		add("filter(%s,%s)",  +[](TtoBool f, TSeq s) -> TSeq { 
			TSeq out;
			for(auto& x : s) {
				if(f(x)) out.push_back(x);
			}
			return out; 
		}); 
		
		// NOTE THIS DOES NOT WORK WITH THE CACHED VERSION
//		add("F(%s,%s)" ,  Builtins::LexiconRecurse<MyGrammar,S>); // note the number of arguments here
	}
		
} grammar;

