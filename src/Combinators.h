#pragma once 

#include "Errors.h"
#include "Grammar.h"
#include "Node.h"

namespace Fleet {
	namespace Combinators {
		
		struct CL {
			
			// must be defined (but not used)
			bool operator<(const CL& other) const { throw NotImplementedError(); }
		
		}; // just a dummy type here
		
		/**
		 * @class SKGrammar
		 * @author piantado
		 * @date 29/04/20
		 * @file Combinators.h
		 * @brief A grammar for SK combinatory logic. NOTE: CustomOps must be defined here. 
		 */
		class SKGrammar : public Grammar<CL> { 
			using Super=Grammar<CL>;
			using Super::Super;
			
			public:
			SKGrammar() : Grammar<CL>() {
				add<CL>(BuiltinOp::op_K,                           "K");
				add<CL>(BuiltinOp::op_S,                           "S");
				add<CL, CL, CL>(BuiltinOp::op_SKAPPLY,         "(%s %s)");		
			}
		} skgrammar;

		// Define a function to check whether we can reduce each nodes via each bulit-in op
		template<BuiltinOp> bool can_reduce(const Node& n);

		template<>
		bool can_reduce<BuiltinOp::op_I>(const Node& n) {
			// (I x)
			return n.nchildren() > 0 and 
				   n[0].rule->instr.is_a(BuiltinOp::op_I);
		}
		template<>
		bool can_reduce<BuiltinOp::op_K>(const Node& n) {
			// ((K x) y)
			return n.nchildren() > 0 and 
				   n[0].nchildren() > 0 and
				   n[0][0].rule->instr.is_a(BuiltinOp::op_K);
		}
		template<>
		bool can_reduce<BuiltinOp::op_S>(const Node& n) {
			// (((S x) y) z)
			return n.nchildren() > 0 and 
				   n[0].nchildren() > 0 and
				   n[0][0].nchildren() > 0 and
				   n[0][0][0].rule->instr.is_a(BuiltinOp::op_S);
		}


		/**
		 * @brief check if a tree n is in SK normal form, meaning that no subnodes can be reduced
		 * @param n
		 * @return boolean
		 */
		bool is_normal_form(const Node& n) {
			// Check if n can be reduced at all
			for(const auto& ni : n) {
				if(can_reduce<BuiltinOp::op_I>(ni) or
				   can_reduce<BuiltinOp::op_S>(ni) or
				   can_reduce<BuiltinOp::op_K>(ni)) return false;
			}
			return true;	
		}


				

		/**
		 * @class ReductionException
		 * @author piantado
		 * @date 29/04/20
		 * @file Combinators.h
		 * @brief A class that gets thrown when we've reduced too much 
		 */
		class ReductionException: public std::exception {} reduction_exception;
				
		/**
		 * @brief Reduce a combinator 
		 * @param n - what node do we reduce?
		 * @param remaining_calls - how many calls can we make before throwing a ReductionException
		 */		
		void reduce(Node& n, size_t& remaining_calls) {
			// returns true if we change anything
			
			
			bool modified; // did we change anything?
			do { // do this without recursion since its faster and we don't have the depth limit
			
				// first ensure that my children have all been reduced
				for(size_t c=0;c<n.nchildren();c++){
					reduce(n.child(c), remaining_calls);
				}
				
				
				if(remaining_calls-- == 0) throw reduction_exception;

				modified = false;
				
				// try to evaluate n according to the rules
				if(can_reduce<BuiltinOp::op_I>(n)) {
					auto x = std::move(n[1]);
					n = x;
					modified = true;
				}
				else if(can_reduce<BuiltinOp::op_K>(n)) {
					auto x = std::move(n[0][1]);
					n = x;
					modified = true;
				} 
				else if(can_reduce<BuiltinOp::op_S>(n)) {
					Rule* rule = skgrammar.get_rule(skgrammar.nt<CL>(), BuiltinOp::op_SKAPPLY);
					
					// just make the notation handier here
					auto x = std::move(n[0][0][1] );
					auto y = std::move(n[0][1]);
					auto z = std::move(n[1]);
					Node zz = z; // copy 
					
					Node q(rule); // build our rule
					q.set_child(0, std::move(x)); 
					q.set_child(1, std::move(zz)); //copy
					
					Node r(rule);
					r.set_child(0, std::move(y));
					r.set_child(1, std::move(z));
					
					n.set_child(0,q);
					n.set_child(1,r);
					modified = true;
				}		
				
			} while(modified);
		}


		/**
		 * @brief Reduce (automtically set remaining_calls=256 default)
		 * @param n
		 */
		void reduce(Node& n) {
			size_t remaining_calls = 256;
			
			try {
				reduce(n,remaining_calls);
			} catch(ReductionException& e) {
				n = Node(); // Just zero out
			}
		}


		/**
		 * @brief Apply a function f to a node x and reduce. 
		 * @param f
		 * @param x
		 * @return 
		 */
		Node apply_and_reduce(Node f, Node x){ // important that these are copies, NOT references
			Node n(skgrammar.get_rule("(%s %s)")); // NOTE: This search step is slow and inefficient! TODO: Do something different!
			n.set_child(0, f);
			n.set_child(1, x);
			
			reduce(n);
			
			return n;
		}

		
		/**
		 * @class LazyNormalForms
		 * @author piantado
		 * @date 29/04/20
		 * @file Combinators.h
		 * @brief Use a nice data structure to enumerate the combinators in normal form
				  lazily so that we can do this -- it will store the max enumerated so far,
				  and allow access 
		 */
		class LazyNormalForms {
		public:
			
			size_t idx; // what index are we currently at?
			std::vector<Node> list;
			SKGrammar* grammar;
			
			LazyNormalForms(SKGrammar* g=&skgrammar) : idx(0), grammar(g) { 
			}
			
			Node& at(const size_t i) {
				
				while(list.size() < i+1 and !CTRL_C) {
					auto n = grammar->expand_from_integer(grammar->nt<CL>(), idx++);
					if(is_normal_form(n)) list.push_back(n);
				}
				
				return list.at(i);		
			}
			
			Node& next() {
				return at(list.size());
			}
		};
			
		
		
		
	}
}