#pragma once

#include "SubtreeEnumeration.h"

/**
 * @class FullLZEnumeration
 * @author piantado
 * @date 02/01/21
 * @file FullLZEnumeration.h
 * @brief Enumerate where we may interpret low numbers as pointers back to COMPLETE subtrees. 
 * 		  Note that this enumeration will not be unique since different trees will have multiple
 *        derivations. 
 * 
 * 		  NOTE: We could actually make this prefer larger subtrees if we wanted...
 */
template<typename Grammar_t>
class FullLZEnumeration { 
	Grammar_t* grammar;
	
	
public:
	FullLZEnumeration(Grammar_t* g) : grammar(g) {}
		
	/**
	 * @brief Compute the number of things in n which could be referenced 
	 * 		  Here, we will require references to prior nodes to be unique and complete
	 *        and only of type nt
	 * @param n
	 * @return 
	 */
	size_t compute_possible_referents(const Node& root, const nonterminal_t nt) { 
		size_t cnt = 0;
		std::set<Node> unique;
		for(auto& x : root) {
			if(x.nt() == nt and 
			   x.is_complete() and 
			   (not unique.contains(x)) and 
			   (not x.is_terminal())) {
				   
				cnt++;
				unique.insert(x);
				//CERR "possible reference " TAB cnt TAB x.string() ENDL;
			}
		}
		return cnt; 
	}
	
	Node& get_referent(const Node& root, size_t cnt, const nonterminal_t nt) { 
		std::set<Node> unique;
		for(auto& x : root) {
			if(x.nt() == nt and 
			   x.is_complete() and 
			   (not unique.contains(x)) and 
			   (not x.is_terminal())) {
	
				// if we found it!
				if(cnt == 0) return x;
				
				cnt--;
				unique.insert(x);
			}
		}
		assert(false && "*** You asked for a referent that was impossible!");
	}

	/**
	 * @brief Convert to a node, using root as the root -- NOTE that when root is null, we use out as the root
	 * @param nt
	 * @param is
	 * @return 
	 */
	[[nodiscard]] virtual Node toNode(IntegerizedStack& is, const nonterminal_t nt, Node& root) {
		
		// NOTE: The order of the next two chunks is important. If we check subtrees first, then we will
		// enumerate references to subtrees before other terminals. If we flip the order, we get
		// other terminals first. 
		
		// otherwise let's see if it's a reference to a prior subtree of type nt
		size_t possible_referents = compute_possible_referents(root, nt); // what could we have referenced?
		if(is.get_value() < possible_referents) {
			return get_referent(root, is.get_value(), nt);
		}
		is -= possible_referents;		
		
		// if we encode a terminal
		enumerationidx_t numterm = this->grammar->count_terminals(nt);
		if(is.get_value() < numterm) {
			return this->grammar->makeNode(this->grammar->get_rule(nt, is.get_value()));	// whatever terminal we wanted
		}
		is -= numterm;
		
		// otherwise we unpack the kids' encoding:			
		auto ri = is.pop(this->grammar->count_nonterminals(nt)); // this already includes the shift from all the nonterminals
		Rule* r = this->grammar->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
		Node out = this->grammar->makeNode(r);
		int i=0;
		for(auto v : is.split(r->N)) {
			
			// we assume we get a null root when root starts out -- so in that case, 
			// out is the root for everything below. 
			if(root.is_null()) {
				out.set_child(i, toNode(v, r->type(i), out) ) ; 
			}
			else {
				out.set_child(i, toNode(v, r->type(i), root) ) ; 
			}
			
			i++;
		}
	//	CERR "\t" << root TAB is TAB out ENDL;
			
		return out;					
		
	}

	// Required here or else we get z converted implicity to IntegerizedStack
	[[nodiscard]] virtual Node toNode(enumerationidx_t z, const nonterminal_t nt, Node& root) {
		IntegerizedStack is(z);
		return toNode(is, nt, root);
	}
	
		// Required here or else we get z converted implicity to IntegerizedStack
	[[nodiscard]] virtual Node toNode(enumerationidx_t z, const nonterminal_t nt) {
		// defaultly create a null node to pass in.
		Node n;
		
		IntegerizedStack is(z);
		return toNode(is, nt, n);
	}
	


	[[nodiscard]] virtual enumerationidx_t toInteger(const Node& root, const Node& n) {
		assert(0);		
	}

};


