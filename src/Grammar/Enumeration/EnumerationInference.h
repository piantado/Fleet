#pragma once

#include "FleetStatistics.h"
#include "IntegerizedStack.h"

/**
 * @class EnumerationInterface
 * @author piantado
 * @date 02/01/21
 * @file EnumerationInterface.h
 * @brief This interface an easy enumeratino interface which provides an abstract class for various
 * 		  types of enumeration. Most need to know about the grammar type.  
 * 
 * 		  NOTE: This is NOT the inference scheme -- for that see Inference/Enumeration.h
 */
template<typename Grammar_t, typename... ARGS>
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
	virtual Node toNode(IntegerizedStack& is, ARGS... args)  {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	
	/**
	 * @brief Expand an integer to a node using this nonterminal type. 
	 * @param nt
	 * @param z
	 * @return 
	 */
	virtual Node toNode(enumerationidx_t z, ARGS... args) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(is, args...);
	}
	
	/**
	 * @brief Convert a node to an integer (should be the inverse of toNode)
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const Node& n, ARGS... args) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&)");
	}
	
};

