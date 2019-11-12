#pragma once

#include <string>
#include <cstdlib>
#include <functional>
#include <tuple>
#include <assert.h>

#include "Instruction.h"
#include "TemplateMagic.h"

struct PrePrimitive {
	// need to define something all Primitives inherit from so that
	// we can share op_counters across them.
	// op_counter here is what we use to assign each Primitive a unique PrimitiveOp number
	static PrimitiveOp op_counter; 
};
PrimitiveOp PrePrimitive::op_counter = 0;

//// so we can give it a lambda of VMS if we wanted
template<typename T, typename... args> // function type
struct Primitive : PrePrimitive {
	// A primitive associates a string name (format) with a function, 
	// and allows grammars to extract all the relevant function pieces via constexpr,
	// and also defines a VMS function that can be called in dispatch
	// op here is generated uniquely for each Primitive, which is how LOTHypothesis 
	// knows which to call. 
	// SO: grammars use this op (statically updated) in nodes, and then when they 
	// are linearized the op is processed in a switch statement to evaluate
	// This is a funny intermediate type that essentially helps us associate 
	// functions, formats, and nonterminal types all together. 
	
	std::string format;
	T(*call)(args...); 
	PrimitiveOp op;
	double p;

	Primitive(std::string fmt, T(*f)(args...), double _p=1.0 ) :
		format(fmt), call(f), op(op_counter++), p(_p) {
			
		// check that each type here is in FLEET_GRAMMAR_TYPES
		// or else we get obscure errors;
		static_assert(contains_type<typename std::decay<T>::type,FLEET_GRAMMAR_TYPES>(), 
					  "*** Type is not in FLEET_GRAMMAR_TYPES");
		
		static_assert((contains_type<typename std::decay<args>::type,FLEET_GRAMMAR_TYPES>() && ...), 
					  "*** Type is not in FLEET_GRAMMAR_TYPES");
		
		// Next, we check reference types. We are allowed to have a reference in a primitive 
		// which allows us to modify a value on the stack without copying it out. This tends
		// to be faster for some data types. The requirements are:
		// (i) there is only one reference type (since it corresponds to the return type), and 
		// (ii) the reference is the FIRST argument to the function. This is because the first argument is
		//		typically the last to be evaluated (TODO: We should fix this in the future because its not true on all compilers)
		
		
		// TODO: ALSO ASSERT that the return type is the same as the reference (otherwise the grammar doesn't know what ot do)
		// BUT Note we shouldn't actually return
		if constexpr(sizeof...(args) > 0) {
			static_assert(CountReferences<args...>::value <= 1, "*** Cannot contain more than one reference in arguments, since the reference is where we put the return value.");
			static_assert(CheckReferenceIsFirst<args...>::value, "*** Reference must be the first argument so it will be popped from the stack last (in fact, it is left in place).");
 		}
//		static_assert(CountReferences<args...>::value == 0 or std::is_void<T>::value, "*** If you use a reference, returntype must be the same -- though note you won't actually return anything");
		
	}

	
	template<typename V>
	vmstatus_t VMScall(V* vms) {
		// This is the default way of evaluating a PrimitiveOp
		
		assert(not vms->template any_stacks_empty<typename std::decay<args>::type...>() &&
				"*** Somehow we ended up with empty arguments -- something is deeply wrong and you're in big trouble."); 
		
		// We let the compiler decide whether to push or not -- there is no push if we have a reference arg,
		// since that is left on the stack and we modify it. 		
		if constexpr (sizeof...(args) == 0) {
			// simple -- no arguments, no references, etc .
			vms->push(this->call());
		}
		else {
			// deal with those references etc. 
			if constexpr (CountReferences<args...>::value == 0) {
				// if its not a reference, we just call normally
				vms->push(this->call(vms->template get<args>()...));
			}
			else { 
				// don't push -- we assuming the reference handled the return value
				// NOTE: here the order of evaluation of function arguments is RIGHT to LEFT
				// which is why we require references in the way that we do (see Fleet.h)
//				|
//				|
//				|
//				|
//				|
//				|
//				|
//				|
//				|	
//				|
//				|
//				|
//				|
//				|
//				|	
//				|
//				|
//				|
//				|
//				|
//				|	
//				|
//				|
//				|				
				// TODO: ENSURE ORDER OF CALLS.... GCC does it right-to-left
				// but this is not specified in the standard
				this->call(std::forward<args>(vms->template get<args>())...);
			}
		}
		return vmstatus_t::GOOD;
	}
	
};




// This is some real template magic that lets us index into "PRIMITIVES" 
// by index at runtime to call vsm. 
// For instance, in LOTHypothesis this is used as
//	abort_t dispatch_rule(Instruction i, VirtualMachinePool<Object, bool>* pool, VirtualMachineState<Object,bool>& vms, Dispatchable<Object,bool>* loader ) {
//		return apply(PRIMITIVES, (size_t)i.getCustom(), &vms);
// (Otherwise you can't index into a tuple at runtime)
// This is from here:
// https://stackoverflow.com/questions/21062864/optimal-way-to-access-stdtuple-element-in-runtime-by-index
namespace Fleet::applyVMS { 
	
	template<int... Is>        struct seq {};
	template<int N, int... Is> struct gen_seq : gen_seq<N-1, N-1, Is...> {};
	template<int... Is>        struct gen_seq<0, Is...> : seq<Is...> {};

	template<int n, class T, class V>
	vmstatus_t applyToVMS_one(T& p, V vms) {
		return std::get<n>(p).VMScall(vms);
	}

	template<class T, class V, int... Is>
	vmstatus_t applyToVMS(T& p, int index, V func, seq<Is...>) {
		using FT = vmstatus_t(T&, V);
		static constexpr FT* arr[] = { &applyToVMS_one<Is, T, V>... };
		return arr[index](p, func);
	}
}
template<class T, class V>
vmstatus_t applyToVMS(T& p, int index, V vms) {
//    return Fleet::applyVMS::applyToVMS(p, index, vms, std::make_index_sequence<std::tuple_size<T>::value>{} );
    return Fleet::applyVMS::applyToVMS(p, index, vms, Fleet::applyVMS::gen_seq<std::tuple_size<T>::value>{});
	// TODO: I think we can probably get away without gen_seq, using something like below
//    return Fleet::applyVMS::applyToVMS(p, index, vms, std::make_integer_sequence<std::tuple_size<T>>{});

}