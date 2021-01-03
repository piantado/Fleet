#pragma once

#include "EnumerationInference.h"

template<typename Grammar_t>
class GrammarEnumeration : public EnumerationInterface<Grammar_t,nonterminal_t> {
	using Super = EnumerationInterface<Grammar_t,nonterminal_t>;
public:
	GrammarEnumeration(Grammar_t* g) : Super(g) {}
	
	/**
	 * @brief This is a handy function to enumerate trees produced by the grammar. 
	 *  This works by encoding each tree into an integer using some "pairing functions" 
	 *  (see https://arxiv.org/pdf/1706.04129.pdf)
	 * 	Specifically, here is how we encode a tree: if it's a terminal,
	 * 	encode the index of the terminal in the grammar. 
	 * 	If it's a nonterminal, encode the rule with a modular pairing function
	 * 	and then use Rosenberg-Strong encoding to encode each of the children. 
	 * 	In this way, we can make a 1-1 pairing between trees and integers,
	 * 	and therefore store trees as integers in a reversible way
	 * 	as well as enumerate by giving this integers. 
	 */
	virtual Node toNode(const nonterminal_t& nt, IntegerizedStack& is) override {
	
		// NOTE: for now this doesn't work when nt only has finitely many expansions/trees
		// below it. Otherwise we'll get an assertion error eventually.
		enumerationidx_t numterm = this->grammar->count_terminals(nt);
		if(is.get_value() < numterm) {
			return this->grammar->makeNode(this->grammar->get_rule(nt, is.get_value()));	// whatever terminal we wanted
		}
		else {
			// Otherwise it is encoding numterm+which_terminal
			is -= numterm; 
			
			auto ri = is.pop(this->grammar->count_nonterminals(nt)); // this already includes the shift from all the nonterminals
			
			Rule* r = this->grammar->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
			Node out = this->grammar->makeNode(r);
			for(size_t i=0;i<r->N;i++) {
				// note that this recurses on the enumerationidx_t format -- so it makes a new IntegerizedStack for each child
				out.set_child(i, toNode(r->type(i), i==r->N-1 ? is.get_value() : is.pop()) ) ; 
			}
			
			return out;					
		}
	}
	
	[[nodiscard]] virtual Node toNode(const nonterminal_t& frm, enumerationidx_t z) override {
		return Super::toNode(frm,z);
	}
	
	virtual enumerationidx_t toInteger(const nonterminal_t& __ignore, const Node& n) override {
		// inverse of the above function -- what order would we be enumerated in?
		if(n.nchildren() == 0) {
			return this->grammar->get_index_of(n.rule);
		}
		else {
			// here we work backwards to encode 
			nonterminal_t nt = n.rule->nt;
			enumerationidx_t numterm = this->grammar->count_terminals(nt);
			
			IntegerizedStack is(toInteger(nt, n.child(n.rule->N-1)));
			for(int i=n.rule->N-2;i>=0;i--) {
				is.push(toInteger(nt, n.child(i)));
			}
			is.push(this->grammar->get_index_of(n.rule)-numterm, this->grammar->count_nonterminals(nt));
			is += numterm;
			return is.get_value();
		}
	}
};

