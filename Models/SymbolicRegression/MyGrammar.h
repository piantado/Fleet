#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Singleton.h"
#include "Grammar.h"
#include "ConstantContainer.h"

const double TERMINAL_P = 2.0;

struct TooManyConstantsException : public VMSRuntimeError {};


class MyGrammar : public Grammar<X_t,D, D,X_t>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		add("(%s+%s)",    +[](D a, D b) -> D     { return a+b; });
		add("(%s-%s)",    +[](D a, D b) -> D     { return a-b; });
		add("(%s*%s)",    +[](D a, D b) -> D     { return a*b; });
		add("(%s/%s)",    +[](D a, D b) -> D     { return a/b; });
		add("(-%s)",      +[](D a)          -> D { return -a; });
		
		add("exp(%s)",     +[](D a)          -> D { return exp(a); }, 1);
		add("log(%s)",    +[](D a)          -> D { return log(a); });
	
		add("1",          +[]()             -> D { return 1.0; }, TERMINAL_P);
		add("sqrt(%s)",    +[](D a)          -> D { return std::sqrt(a); });
	
#if FEYNMAN

		add("tau",         +[]()             -> D { return 2*M_PI; }, TERMINAL_P); // pi is for losers
		add("0.5",          +[]()           -> D { return 0.5; }, TERMINAL_P);
		
		add("pow(%s,2)",   +[](D a)          -> D { return a*a; }, 1./2);
		add("pow(%s,3)",   +[](D a)          -> D { return a*a*a; }, 1./2);
		
		add("tanh(%s)",    +[](D a)          -> D { return tanh(a); }, 1./5);
		add("sin(%s)",    +[](D a)          -> D { return sin(a); }, 1./5);
		add("cos(%s)",    +[](D a)          -> D { return cos(a); }, 1./5);
		add("asin(%s)",    +[](D a)         -> D { return asin(a); }, 1./5);
		add("acos(%s)",    +[](D a)         -> D { return acos(a); }, 1./5);
		
#else
		// we're only going to use unrestricted pow in non-FEYNMAN cases, just for simplicity
		add("pow(%s,%s)", +[](D a, D b)     -> D { return pow(a,b); });
	
		// give the type to add and then a vms function
		add_vms<D>("C", new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
						
				// Here we are going to use a little hack -- we actually know that vms->program_loader
				// is of type MyHypothesis, so we will cast to that
				auto* h = dynamic_cast<ConstantContainer*>(vms->program.loader);
				if(h == nullptr) { assert(false); }
				else {
					// now we might have too many since the size of our constants is constrained
					if(h->constant_idx >= h->constants.size()) { 
						throw TooManyConstantsException();
					}
					else { 
						vms->template push<D>(h->constants.at(h->constant_idx++));
					}
				}
		}), TERMINAL_P);		
#endif 

		// NOTE: These are now added in main so we can dynamically have different numbers of arguments
		// and display them in a friendly way 
		//add("x",             Builtins::X<MyGrammar>, TERMINAL_P);
		//add("%s1", 			+[](X_t x) -> D { return x[1]; }, TERMINAL_P);
		
	}					  
					  
} grammar;

// check if a rule is constant 
bool isConstant(const Rule* r) { return r->format == "C"; }
