#pragma once

#include "SubtreeEnumeration.h"

/**
 * @class PartialLZEnumeration
 * @author piantado
 * @date 02/01/21
 * @file PartialLZEnumeration.h
 * @brief Same as FullLZEnumeration except that the pointers to previous trees are to *partial* 
 * 		  trees, not full ones. 
 */
template<typename Grammar_t>
class PartialLZEnumeration { 
	Grammar_t* grammar;
	SubtreeEnumeration<Grammar_t> st;
	
	// when we enumerate partial trees, skip ones with more than this may subtrees
	size_t max_partial_enum = 1000; 
	size_t min_ref_size = 4; // don't reference pieces unless they have this many nodes (counting nulls!)
	
public:
	PartialLZEnumeration(Grammar_t* g) : grammar(g), st(grammar) {}
		
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
		for(auto& n : root) {
			
			auto nx = st.count(n);
			if(nx > max_partial_enum) continue; // don't spend all our time here
			for(size_t i=0;i<nx;i++) {
				auto x = st.toNode(i,root);
			
				if(x.nt() == nt and (not unique.contains(x)) and x.count_nonnull() > min_ref_size and (not x.is_null())) {
					cnt++;
					unique.insert(x);
					//CERR "possible reference " TAB cnt TAB x.string() ENDL;
				}
			}
		}
		return cnt; 
	}
	
	Node get_referent(const Node& root, size_t cnt, const nonterminal_t nt) { 
		std::set<Node> unique;
		for(auto& n : root) {
			
			auto nx = st.count(n);
			if(nx > max_partial_enum) continue; // don't spend all our time here
			for(size_t i=0;i<nx;i++) {
				auto x = st.toNode(i,root);
			
				if(x.nt() == nt and (not unique.contains(x)) and x.count_nonnull() > min_ref_size and (not x.is_null())) {
					   
					if(cnt == 0) {
						return x;
					}
					
					cnt--;
					unique.insert(x);
					//CERR "possible reference " TAB cnt TAB x.string() ENDL;
				}
			}
		}
		CERR root TAB cnt TAB nt ENDL;
		assert(false && "*** You asked for a referent that was impossible!");
	}

	/**
	 * @brief Convert to a node, using root as the root -- NOTE that when root is null, we use out as the root
	 * @param nt
	 * @param is
	 * @return 
	 */
	[[nodiscard]] virtual Node toNode(IntegerizedStack& is, const nonterminal_t nt, Node& root) {
		
		// NOTE: These chunks can't have the partial ones first (as in FullLZEnumeration) because
		// because the partial ones require more expansion after. 
		
		// if we encode a terminal
		enumerationidx_t numterm = this->grammar->count_terminals(nt);
		if(is.get_value() < numterm) {
			return this->grammar->makeNode(this->grammar->get_rule(nt, is.get_value()));	// whatever terminal we wanted
		}
		is -= numterm;

		// otherwise it's either a reference to a prior node or not
		auto possible_referents = compute_possible_referents(root, nt);
		if(possible_referents > 0 and is.pop(2)==0) {
			auto n = get_referent(root, is.pop(possible_referents), nt);
			
			// let's set this feature so we can see in debugging
			//for(auto& x : n) { x.can_resample = false; }
			
			// and now we need to fill in the nodes
			for(auto& x : n) {
				if(x.is_null()) {
					x.assign(toNode(is, x.parent->type(x.pi), root));
				}
			}

			assert(n.is_complete());			
			return n; 
		}
		else {
				
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

			assert(out.is_complete());							
			return out;					
		}
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


