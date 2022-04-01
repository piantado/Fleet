
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Singleton.h"
#include "Grammar.h"

const double TERMINAL_P = 2.0;

class MyGrammar : public Grammar<X_t,D, D,X_t>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		add("(%s+%s)",    +[](D a, D b) -> D     { return a+b; }),
		add("(%s-%s)",    +[](D a, D b) -> D     { return a-b; }),
		add("(%s*%s)",    +[](D a, D b) -> D     { return a*b; }),
		add("(%s/%s)",    +[](D a, D b) -> D     { return (b==0 ? NaN : a/b); }),
		
		add("1",          +[]()             -> D { return 1.0; }, TERMINAL_P),
		
#if FEYNMAN

		add("tau",         +[]()             -> D { return 2*M_PI; }, TERMINAL_P);
		add("sqrt(%s)",    +[](D a)          -> D { return std::sqrt(a); }, 1.),
		add("pow(%s,2)",   +[](D a)          -> D { return a*a; }, 1.),
		add("pow(%s,3)",   +[](D a)          -> D { return a*a*a; }, 1.),
		add("expm(%s)",    +[](D a)          -> D { return exp(-a); }, 1),

		add("0.5",          +[]()           -> D { return 0.5; }, TERMINAL_P),
//		add("pi",          +[]()            -> D { return M_PI; }),
		add("sin(%s)",    +[](D a)          -> D { return sin(a); }, 1./3),
		add("cos(%s)",    +[](D a)          -> D { return cos(a); }, 1./3),
		add("asin(%s)",    +[](D a)         -> D { return asin(a); }, 1./3);

#else
		add("(-%s)",      +[](D a)          -> D { return -a; }),
		add("exp(%s)",    +[](D a)          -> D { return exp(a); }),
		add("log(%s)",    +[](D a)          -> D { return log(a); }),
		add("pow(%s,%s)", +[](D a, D b)     -> D { return pow(a,b); }),
				
		// give the type to add and then a vms function
		add_vms<D>("C", new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
						
				// Here we are going to use a little hack -- we actually know that vms->program_loader
				// is of type MyHypothesis, so we will cast to that
				auto* h = dynamic_cast<ConstantContainer*>(vms->program.loader);
				if(h == nullptr) { assert(false); }
				else {
					assert(h->constant_idx < h->constants.size()); 
					vms->template push<D>(h->constants.at(h->constant_idx++));
				}
		}), TERMINAL_P);		
#endif 

//		add("x",             Builtins::X<MyGrammar>, TERMINAL_P);
//		add("%s1", 			+[](X_t x) -> D { return x[1]; }, TERMINAL_P);
		
	}					  
					  
} grammar;

// check if a rule is constant 
bool isConstant(const Rule* r) { return r->format == "C"; }
