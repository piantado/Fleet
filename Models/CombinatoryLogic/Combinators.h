#pragma once 

#include <signal.h>

#include "Errors.h"
#include "Grammar.h"
#include "Node.h"
#include "Builtins.h"

#include "SExpression.h"

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
		
	/**
	 * @brief Combinator reduction, assuming that n is a SExpNode
	 * @param n
	 * @param remaining_calls
	 */
	void reduce(SExpression::SExpNode& n, int& remaining_calls) {
		// returns true if we change anything
		
		bool modified; // did we change anything?
		do { // do this without recursion since its faster and we don't have the depth limit
			modified = false;
		
			// first ensure that my children have all been reduced
			for(size_t c=0;c<n.nchildren();c++){
				reduce(n.child(c),remaining_calls);
			}
		
	//			std::string original = string();
		//	::print("REDUCE", n.string());
			
			if(remaining_calls-- == 0)
				throw Combinators::reduction_exception;

			// try to evaluate n according to the rules
			const auto NC = n.nchildren();
			assert(NC <= 2); // we don't handle this -- 3+ children should have been caught in the constructor 
			
			if(NC == 2 and not n.label.has_value()) { // we are an application node
				if(n.child(0).get_label() == "I"){ 				// (I x) = x
					assert(n.child(0).nchildren() == 0);
					auto x = std::move(n.child(1));
					n = x; 
					modified = true;
	//					::print("HERE I", string());
				}
				else if(n.child(0).nchildren() == 2 and
						n.child(0).child(0).get_label() == "K") {	// ((K x) y) = x
					assert(n.child(0).child(0).nchildren() == 0);
					auto x = std::move(n.child(0).child(1));
					n = x; 
	//					::print("HERE K", string());
					modified = true;
				} 
				else if(n.child(0).nchildren() == 2 and
						n.child(0).child(0).nchildren() == 2 and
						n.child(0).child(0).child(0).get_label() == "S") { // (((S x) y) z) = ((x z) (y z))
					assert(n.child(0).child(0).child(0).nchildren() == 0);
					
					// just make the notation handier here
					auto x = std::move(n.child(0).child(0).child(1));
					auto y = std::move(n.child(0).child(1));
					auto z = std::move(n.child(1));
					SExpression::SExpNode zz = z; // copy 
					
					SExpression::SExpNode q; // build our rule
					q.set_child(0, std::move(x)); 
					q.set_child(1, std::move(zz)); //copy
					
					SExpression::SExpNode r;
					r.set_child(0, std::move(y));
					r.set_child(1, std::move(z));
					
					n.label.reset(); // clear the label value since I'm an apply now
					n.set_child(0,std::move(q));
					n.set_child(1,std::move(r));
					modified = true;
	//					::print("HERE S", string());
				}
			}
			else if( (not n.label.has_value()) and n.nchildren() == 1) {
				auto x = std::move(n.child(0));
				n = x; // ((x)) = x
				modified = true; 
	//				::print("HERE APP", string());
			}
			
	//			::print("GOT", this, label, original, string());
			
			
		} while(modified); // end while
	}

	void reduce(SExpression::SExpNode& n) {
		int remaining_calls = 256;
		reduce(n,remaining_calls);
	}

	SExpression::SExpNode NodeToSExpNode(const Node& n) {
		Rule* rapp = Combinators::skgrammar.get_rule(Combinators::skgrammar.nt<CL>(), "(%s %s)");
		
		if(n.rule == rapp) {
			SExpression::SExpNode out;
			for(size_t i=0;i<n.nchildren();i++){
				out.set_child(i, NodeToSExpNode(n.child(i)));
			}
			return out; 
		}
		else {
			assert(n.nchildren() == 0); // must be a terminal 
			return SExpression::SExpNode(n.format()); // must match ISK
		}
		
	}

	Node SExpNodeToNode(const SExpression::SExpNode& n) {	
		if(n.nchildren() == 0) {
			assert(n.label.has_value());
			Rule* rl = Combinators::skgrammar.get_rule(Combinators::skgrammar.nt<CL>(), n.label.value());
			return Node(rl);
		}
		else if(n.nchildren() == 1) {
			return SExpNodeToNode(n.child(0));		
		}
		else if(n.nchildren() == 2) {
			Rule* rapp = Combinators::skgrammar.get_rule(Combinators::skgrammar.nt<CL>(), "(%s %s)");
			assert(rapp != nullptr);
			Node out(rapp);
			for(size_t i=0;i<n.nchildren();i++){
				out.set_child(i, SExpNodeToNode(n.child(i)));
			}
			return out; 
		}
		else {
			assert(false); 
		}
	}

	template<typename L>
	void substitute(SExpression::SExpNode& n, const L& m) {
		//::print(n.string(), "LABEL=",n.get_label(), m.factors.contains(n.get_label()));
		
		if(n.label.has_value() and m.factors.contains(n.label.value())) {
			auto v = m.at(n.label.value()).get_value(); // copy
			n = NodeToSExpNode(v);
		}
		
		for(auto& c : n.get_children()) {
			substitute(c, m);
		}
	}
	
}

