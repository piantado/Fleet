#pragma once

#include <vector>
#include "Instruction.h"

template<typename T>
void increment(std::vector<T>& v, size_t idx, T count=1) {
	// Increment and potentially increase my vector size if needed
	if(idx > v.size()) 
		v.resize(idx+1,0);
	v[idx] += count;
}

/**
 * @class RuntimeCounter
 * @author Steven Piantadosi
 * @date 04/09/20
 * @file RuntimeCounter.h
 * @brief This class manages counting operations at runtime and interfaces operations to a grammar
 * 			NOTE: Currently we don't track each arg separately, since that's a pain
 */
class RuntimeCounter {
public:

	// counts of each type of primitive
	std::vector<size_t> builtin_count;
	std::vector<size_t> primitive_count;

	// we defaulty initialize these
	RuntimeCounter() : builtin_count(16,0), primitive_count(16,0) {	}
	
	/**
	 * @brief Add count number of items to this instruction's count
	 * @param i
	 */	
	void increment(Instruction& i, size_t count=1) {
		//CERR ">>" TAB builtin_count.size() TAB primitive_count.size() TAB i TAB this ENDL;
		
		if(i.is<BuiltinOp>()) {
			increment(builtin_count, (size_t)i.as<BuiltinOp>(), 1);
		}
		else {
			increment(primitive_count, (size_t)i.as<PrimitiveOp>(), 1);
		}
	}
		
	/**
	 * @brief Add the results of another runtime counter
	 * @param rc
	 */	
	void increment(RuntimeCounter& rc) {
		for(size_t i=0;i<rc.builtin_count.size();i++) {
			increment(builtin_count, i, rc.builtin_count[i]);
		}
		for(size_t i=0;i<rc.primitive_count.size();i++) {
			increment(primitive_count, i, rc.primitive_count[i]);
		}
	}
		
	/**
	 * @brief Retrieve the rule count for a given instruction
	 * @param i
	 * @return 
	 */
	// retrieve the count corresponding to some grammar rule
	size_t get(Instruction& i) {
		if(i.is<BuiltinOp>()) {
			auto idx = (size_t)i.as<BuiltinOp>();
			if(idx >= builtin_count.size()) return 0;
			else				     return builtin_count[idx];
		}
		else {
			auto idx = (size_t)i.as<PrimitiveOp>();
			if(idx >= primitive_count.size()) return 0;
			else				       return primitive_count[idx];
		}
	}

	
	
	/**
	 * @brief Display a runtime counter -- NOTE This may not display all zeros if instructions have not been run
	 * @return 
	 */	
	std::string string() const {
		std::string out = "< ";
		for(auto& n : builtin_count) {
			out += str(n) + " ";
		}
		out += " : ";
		for(auto& n : primitive_count) {
			out += str(n) + " ";
		}
		out += ">";
		return out;
	}

};