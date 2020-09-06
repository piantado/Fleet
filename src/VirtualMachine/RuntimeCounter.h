#pragma once

#include <atomic>
#include <vector>
#include "Instruction.h"
#include "Miscellaneous.h"

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
	using T = unsigned long;

	// counts of each type of primitive
	std::vector<T> builtin_count;
	std::vector<T> primitive_count;

	T total; // overall count of everything

	// we defaulty initialize these
	RuntimeCounter() : builtin_count(16,0), primitive_count(16,0), total(0) {	}
	
	/**
	 * @brief Add count number of items to this instruction's count
	 * @param i
	 */	
	void increment(Instruction& i, T count=1) {
		//CERR ">>" TAB builtin_count.size() TAB primitive_count.size() TAB i TAB this ENDL;
		total += count;
		if(i.is<BuiltinOp>()) {
			// this general vector increment lives in miscellaneous
			::increment(builtin_count, (size_t)i.as<BuiltinOp>(), (T)count);
		}
		else {
			::increment(primitive_count, (size_t)i.as<PrimitiveOp>(), (T)count);
		}
	}
		
	/**
	 * @brief Add the results of another runtime counter
	 * @param rc
	 */	
	void increment(RuntimeCounter& rc) {
		for(size_t i=0;i<rc.builtin_count.size();i++) {
			::increment(builtin_count, i, (T)rc.builtin_count[i]);
		}
		for(size_t i=0;i<rc.primitive_count.size();i++) {
			::increment(primitive_count, i, (T)rc.primitive_count[i]);
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