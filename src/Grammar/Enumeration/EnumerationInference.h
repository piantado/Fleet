#pragma once

#include "FleetStatistics.h"
#include "IntegerizedStack.h"

/**
 * @class EnumerationInterface
 * @author piantado
 * @date 02/01/21
 * @file Enumeration.h
 * @brief This interface an easy enumeratino interface which provides an abstract class for various
 * 		  types of enumeration. Most need to know about the grammar type.  
 * 
 * 		   from_t is very odd -- might be too clever to use -- basically each kind of enumeration neesd
 *        a little more information. SubtreeEnumeration needs a node to get subtrees from,
 *        GrammarEnumeration needs a nonterminal type, etc. So that is set as from_t and passed in
 *        to toNode. This prevents us from having to have different types in the arguments for toNode
 * 
 * 		  NOTE: This is NOT the infernece scheme -- for that see Inference/Enumeration.h
 */
template<typename Grammar_t, typename from_t>
class EnumerationInterface {
public:
	Grammar_t* grammar;

	EnumerationInterface(Grammar_t* g) : grammar(g) {

	}

	/**
	 * @brief Subclass must implement for IntegerizedStack
	 * @param nt
	 * @param is
	 * @return 
	 */
	virtual Node toNode(const from_t& frm, IntegerizedStack& is)  {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	
	/**
	 * @brief Expand an integer to a node using this nonterminal type. 
	 * @param nt
	 * @param z
	 * @return 
	 */
	virtual Node toNode(const from_t& frm, enumerationidx_t z) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(frm, is);
	}
	
	/**
	 * @brief Convert a node to an integer (should be the inverse of toNode)
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const from_t& frm, const Node& n) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&)");
	}
	
};

