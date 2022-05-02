#pragma once 


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,     S,char,bool,double,StrSet,int,size_t>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		// TODO: We want here some stochastic version of replace, that maybe tkaes
		// a string and replaces one character with an unused one. 
		
		
		add("substitute(%s,%s)", substitute);
		
		add("tail(%s)", +[](S s) -> S { 
			if(s.length()>0) 
				s.erase(0); 
			return s;
		});
				
		add_vms<S,S,S>("append(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			S b = vms->getpop<S>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + b.length() > max_length) throw VMSRuntimeError();
			else 									 a += b; 
		}));
		
		add_vms<S,S,char>("pair(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			char b = vms->getpop<char>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + 1 > max_length) throw VMSRuntimeError();
			else 							a += b; 
		}));

//		add("c2s(%s)", +[](char c) -> S { return S(1,c); });
		add("head(%s)", +[](S s) -> S { return (s.empty() ? EMPTY_STRING : S(1,s.at(0))); }); // head here could be a char, except that it complicates stuff, so we'll make it a string str
		add("\u00D8", +[]() -> S { return EMPTY_STRING; }, CONSTANT_P);
		add("(%s==%s)", +[](S x, S y) -> bool { return x==y; });
		add("empty(%s)", +[](S x) -> bool { 	return x.length()==0; });
		
		add("insert(%s,%s)", +[](S x, S y) -> S { 
			size_t l = x.length();
			if(l == 0) 
				return y;
			else if(l + y.length() > max_length) 
				throw VMSRuntimeError();
			else {
				// put y into the middle of x
				size_t pos = l/2;
				S out = x.substr(0, pos); 
				out.append(y);
				out.append(x.substr(pos));
				return out;
			}				
		});
		
		
		// add an alphabet symbol (\Sigma)
		add("\u03A3", +[]() -> StrSet {
			StrSet out; 
			for(const auto& a: alphabet) {
				out.emplace(1,a);
			}
			return out;
		}, CONSTANT_P);
		
		// set operations:
		add("{%s}", +[](S x) -> StrSet  { 
			StrSet s; s.insert(x); return s; 
		}, CONSTANT_P);
		
		add("(%s\u222A%s)", +[](StrSet s, StrSet x) -> StrSet { 
			for(auto& xi : x) {
				s.insert(xi);
				if(s.size() > max_setsize) throw VMSRuntimeError();
			}
			return s;
		});
		
		add("(%s\u2216%s)", +[](StrSet s, StrSet x) -> StrSet {
			StrSet output; 
			
			// this would usually be implemented like this, but it's overkill (and slower) because normally 
			// we just have single elemnents
			std::set_difference(s.begin(), s.end(), x.begin(), x.end(), std::inserter(output, output.begin()));
			
			return output;
		});
		
		
		add("repeat(%s,%s)", +[](const S a, size_t i) -> S {
			if(a.length()*i > max_length) throw VMSRuntimeError();
			
			S out = a;
			for(size_t j=1;j<i;j++) 
				out = out + a;
			return out;				
		});
		add("len(%s)", +[](const S a) -> size_t { return a.length(); });
		
		for(size_t i=1;i<10;i++) {
			add_terminal(str(i), i, CONSTANT_P/10.0);
		}
			
		add("sample(%s)",     Builtins::Sample<MyGrammar, S>, CONSTANT_P);
		add("sample_int(%s)", Builtins::Sample_int<MyGrammar>, CONSTANT_P);
		
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("x",             Builtins::X<MyGrammar>, CONSTANT_P);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,StrSet>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,double>);

		add("flip(%s)",      Builtins::FlipP<MyGrammar>, CONSTANT_P);

		const int pdenom=24;
		for(int a=1;a<=pdenom/2;a++) { 
			std::string s = str(a/std::gcd(a,pdenom)) + "/" + str(pdenom/std::gcd(a,pdenom)); 
			if(a==pdenom/2) {
				add_terminal( s, double(a)/pdenom, CONSTANT_P);
			}
			else {
				add_terminal( s, double(a)/pdenom, 1.0);
			}
		}
		
		add("F%s(%s)" ,  Builtins::LexiconRecurse<MyGrammar,int>, 1./2.);
		add("Fm%s(%s)",  Builtins::LexiconMemRecurse<MyGrammar,int>, 1./2.);
	}
} grammar;
