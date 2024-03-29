#pragma once

#include "BasicEnumeration.h"

/**
 * @class SubtreeEnumeration
 * @author piantado
 * @date 02/01/21
 * @file SubtreeEnumeration.h
 * @brief Enumerate subtrees of a given tree
 */
template<typename Grammar_t>
class SubtreeEnumeration  {
public:
	Grammar_t* grammar;

	SubtreeEnumeration(Grammar_t* g) : grammar(g) {}
		
	/**
	 * @brief How many partial subtrees are there?
	 * @param n
	 * @return 
	 */
	[[nodiscard]] enumerationidx_t count(const Node& n) {
		// how many possible connected, partial subtrees do I have
		// (e.g. including a path back to my root)
				
		if(n.is_null()) 
			return 1; // only one tree there!

		enumerationidx_t k=1; 
		for(const auto& c : n.get_children() ){
			// for each child independently, I can either make them null, or I can include them
			// and choose one of their own subtrees
			k *= count(c); 
		}
		return k+1; // +1 because I could make the root null
	}

	/**
	 * @brief Convert to the is'th partial subtree of frm
	 * @param nt
	 * @param is
	 * @return 
	 */
	[[nodiscard]] virtual Node toNode(IntegerizedStack& is, const Node& frm) {
		if(is.get_value() == 0) {
			return Node();
		}
		else {
			is -= 1; // is encodes tree+1
			Node out = this->grammar->makeNode(frm.rule); // copy the first level
			for(size_t i=0;i<out.nchildren();i++) {
				auto cz = count(frm.child(i));
				out.set_child(i, toNode(is.pop(cz), frm.child(i)));		
			}
			return out;
		}
	}

	// Required here or else we get z converted implicity to IntegerizedStack
	[[nodiscard]] virtual Node toNode(enumerationidx_t z, const Node& frm) {
		IntegerizedStack is(z);
		return toNode(is, frm);
	}
	

	[[nodiscard]] virtual enumerationidx_t toInteger(const Node& n, const Node& frm) {
		if(n.is_null()) {
			return 0;
		}
		else {
			// else it's a real construction
			
			const int N = n.nchildren();
			assert( (int)frm.nchildren() == N);
		
			IntegerizedStack is;
			
			// We have to go in reverse order here, remember, since that's hwo things are unpacked
			for(int i=N-1;i>=0;i--) {
			
				// the most we can pop from a child is the number of partial trees they have
				auto cx = count(frm.child(i));
				auto k = toInteger(n.child(i), frm.child(i));
				
				is.push(k, cx);
				
				//COUT ">>" TAB k TAB cx TAB is.get_value() TAB "[" << i << "] " << n.child(i) << " in " << n.string() ENDL;
			}
			
			// 1+ becuase it wasn't null
			return 1+is.get_value();		
		}
		
	}

};
