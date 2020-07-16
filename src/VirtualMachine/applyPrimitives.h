#pragma once 

#include<tuple>
#include "Instruction.h"
#include "Miscellaneous.h"

// We have to include this after PRIMITIVES has been defined

// This is some real template magic that lets us index into "PRIMITIVES" 
// by index at runtime to call vsm. 
// This is from here:
// https://stackoverflow.com/questions/21062864/optimal-way-to-access-stdtuple-element-in-runtime-by-index
template<int n, class T, typename V, typename P, typename L>
vmstatus_t applyToVMS_one(T& p, V* vms, P* pool, L* loader) {
	return std::get<n>(p).VMScall(vms, pool, loader);
}

template<class T, typename V, typename P, typename L, size_t... Is>
vmstatus_t applyToVMS(T& p, int index, V* vms, P* pool, L* loader, std::index_sequence<Is...>) {
	using FT = vmstatus_t(T&, V*, P*, L*);
	thread_local static constexpr FT* arr[] = { &applyToVMS_one<Is,T,V,P,L>... }; //thread_local here seems to matter a lot
	return arr[index](p, vms, pool, loader);
}

template<typename V, typename P, typename L>
vmstatus_t applyPRIMITIVEStoVMS(int index, V* vms, P* pool, L* loader) {
	
	// We need to put a check in here to ensure that nobody tries to do grammar.add(Primtive(...)) because
	// then it won't be included in our standard PRIMITIVES table, and so it cannot be called in this way
	// This is a problem in the library that should be addressed.
	assert( ((size_t)index < std::tuple_size<decltype(PRIMITIVES)>::value) && 
		"*** You cannot index a higher primitive op than the size of PRIMITIVES. Perhaps you used grammar.add(Primitive(...)), which is not allowed, instead of putting it into the tuple?");
	assert(index >= 0);
	
	if constexpr ( (std::tuple_size<decltype(PRIMITIVES)>::value > 0) ) {
		return applyToVMS(PRIMITIVES, index, vms, pool, loader, std::make_index_sequence<(std::tuple_size<decltype(PRIMITIVES)>::value)>{});
	}
	else {
		UNUSED(vms); UNUSED(pool); UNUSED(loader);
		throw YouShouldNotBeHereError("*** Cannot call applyPRIMITIVEStoVMS without PRIMITIVES defined");
	}
}


