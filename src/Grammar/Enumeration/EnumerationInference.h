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
 * 		  NOTE: This is NOT the infernece scheme -- for that see Inference/Enumeration.h
 */
template<typename Grammar_t>
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
	virtual Node toNode(nonterminal_t nt, IntegerizedStack& is)  {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	virtual Node toNode(nonterminal_t nt, const Node& frm, IntegerizedStack& is) {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	
	// ensure these don't get called 
	//virtual Node toNode(nonterminal_t nt, IntegerizedStack is) = delete;
	//virtual Node toNode(nonterminal_t nt, const Node& frm, IntegerizedStack is) = delete; 
	
	/**
	 * @brief Expand an integer to a node using this nonterminal type. 
	 * @param nt
	 * @param z
	 * @return 
	 */
	virtual Node toNode(nonterminal_t nt, enumerationidx_t z) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(nt, is);
	}
	
	virtual Node toNode(nonterminal_t nt, const Node& frm, enumerationidx_t z) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(nt, frm, is);
	}
	
	/**
	 * @brief Convert a node to an integer (should be the inverse of toNode)
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const Node& n) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&)");
	}
	
	/**
	 * @brief Convert a node to an integer where we come from something
	 * @param frm
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const Node& frm, const Node& n) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&,Node&)");
	}
	
	
};

