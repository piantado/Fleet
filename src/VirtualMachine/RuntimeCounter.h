#pragma once

#include <vector>
#include "Instruction.h"

// This class manages counting operations at runtime
// and interfaces operations to a grammar
// NOTE: Currently we don't track each arg separately, since
// that's a pain
class RuntimeCounter {
public:

	std::vector<size_t> builtin_count;
	std::vector<size_t> primitive_count;

	RuntimeCounter() : builtin_count(32,0), primitive_count(32,0) {	}
	
	void increment(Instruction& i) {
		//CERR ">>" TAB builtin_count.size() TAB primitive_count.size() TAB i TAB this ENDL;
		
		if(i.is<BuiltinOp>()) {
			auto idx = (size_t)i.as<BuiltinOp>();
			if(idx >= builtin_count.size())
				builtin_count.resize(idx+1,0);
			
			builtin_count[idx]++;
		}
		else {
			auto idx = (size_t)i.as<PrimitiveOp>();
			if(idx >= primitive_count.size()) 
				primitive_count.resize(idx+1,0);
			
			primitive_count[idx]++;
		}
	}
	
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
	
};