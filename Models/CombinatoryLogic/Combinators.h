#pragma once 

#include <signal.h>

#include "Errors.h"
#include "Grammar.h"
#include "Node.h"
#include "Builtins.h"

extern volatile sig_atomic_t CTRL_C;

// Funny ordering is requird here because Builtins need these, Grammar needs builtins
namespace Combinators {
	
	// we need a void class to pass to Grammar, but void won't work!
	// So here we make a class we can never instantiate
	struct cl_void {
		cl_void() { assert(false); }
	};
	
	struct CL {		
		// must be defined (but not used)
		bool operator<(const CL& other) const { throw NotImplementedError(); }	
	}; // just a dummy type here for the grammar


	// Combinatory logic operations 
	template<typename Grammar_t>
	Primitive<Combinators::CL> 
	CL_I(Op::CL_I, BUILTIN_LAMBDA {assert(false);});

	template<typename Grammar_t>
	Primitive<Combinators::CL> 
	CL_S(Op::CL_S, BUILTIN_LAMBDA {assert(false);});

	template<typename Grammar_t>
	Primitive<Combinators::CL>
	CL_K(Op::CL_K, BUILTIN_LAMBDA {assert(false);});

	template<typename Grammar_t>
	Primitive<Combinators::CL, Combinators::CL, Combinators::CL> 
	CL_Apply(Op::CL_Apply, BUILTIN_LAMBDA {assert(false);});
	
	/**
	 * @class SKGrammar
	 * @author piantado
	 * @date 29/04/20
	 * @file Combinators.h
	 * @brief A grammar for SK combinatory logic. NOTE: CustomOps must be defined here. 
	 */
	class SKGrammar : public Grammar<cl_void,CL, CL, cl_void> { 
		using Super=Grammar<cl_void,CL,CL,cl_void>;
		using Super::Super;
		
		public:
		SKGrammar() : Super() {
			// These are given NoOps because they are't interpreted in the normal evaluator
			add("I", CL_I<SKGrammar>);
			add("S", CL_S<SKGrammar>);
			add("K", CL_K<SKGrammar>);
			add("(%s %s)", CL_Apply<SKGrammar>, 2.0);		
		}
		
	} skgrammar;
			
	/**
	 * @class ReductionException
	 * @author piantado
	 * @date 29/04/20
	 * @file Combinators.h
	 * @brief A class that gets thrown when we've reduced too much 
	 */
	class ReductionException: public std::exception {} reduction_exception;
	
}

