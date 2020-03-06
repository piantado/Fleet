#pragma once

#include <string>
#include <cstdlib>
#include <functional>
#include <tuple>
#include <assert.h>

#include "Instruction.h"
#include "TemplateMagic.h"

/**
 * @class PrePrimitive
 * @author piantado
 * @date 05/03/20
 * @file Primitives.h
 * @brief A preprimitive is a class that primitives inherit. We use a static op_counter so that at compile time
 * 		  thsi can be used to compute an op number for each separate primitive
 */
struct PrePrimitive {
	static PrimitiveOp op_counter; 
};
PrimitiveOp PrePrimitive::op_counter = 0;



//// so we can give it a lambda of VMS if we wanted

/**
 * @class Primitive
 * @author piantado
 * @date 05/03/20
 * @file Primitives.h
 * @brief A primitive associates a string name (format) with a function, 
 * and allows grammars to extract all the relevant function pieces via constexpr,
 * and also defines a VMS function that can be called in dispatch
 * op here is generated uniquely for each Primitive, which is how LOTHypothesis 
 * knows which to call. 
 * SO: grammars use this op (statically updated) in nodes, and then when they 
 * are linearized the op is processed in a switch statement to evaluate
 * This is a funny intermediate type that essentially helps us associate 
 * functions, formats, and nonterminal types all together. 
 */

template<typename T, typename... args> // function type
struct Primitive : PrePrimitive {
	
	std::string format;
	T(*call)(args...); 
	PrimitiveOp op;
	double p;

	// First we figure out what the return type of the function is. This determines
	// where we put it in the grammar (e.g. which nonterminal it is associated with).
	// When it's a simple function with no reference, this is just T, but when
	// we have a reference, then the reference is the nonterminal type (and
	// remember that the reference must be first!
	// NOTE: we wrap T in head so conditional operates on structs
	typedef typename std::decay<typename HeadIfReferenceElseT<T,args...>::type>::type GrammarReturnType;
									  

	Primitive(std::string fmt, T(*f)(args...), double _p=1.0 ) :
		format(fmt), call(f), op(op_counter++), p(_p) {
			
		// check that each type here is in FLEET_GRAMMAR_TYPES
		// or else we get obscure errors;
		static_assert(contains_type<GrammarReturnType,FLEET_GRAMMAR_TYPES>(), 
					  "*** Type is not in FLEET_GRAMMAR_TYPES");
		
		static_assert((contains_type<typename std::decay<args>::type,FLEET_GRAMMAR_TYPES>() && ...), 
					  "*** Type is not in FLEET_GRAMMAR_TYPES");
		
		// Next, we check reference types. We are allowed to have a reference in a primitive 
		// which allows us to modify a value on the stack without copying it out. This tends
		// to be faster for some data types. The requirements are:
		// (i) there is only one reference type (since it corresponds to the return type), and 
		// (ii) the reference is the FIRST argument to the function. This is because the first argument is
		//		typically the last to be evaluated (TODO: We should fix this in the future because its not true on all compilers)
		// (iii) if we return void, then we must have a reference (for return value) and vice versa
		
		// TODO: ALSO ASSERT that the return type is the same as the reference (otherwise the grammar doesn't know what ot do)
		// BUT Note we shouldn't actually return
		if constexpr(sizeof...(args) > 0) {
			static_assert(CountReferences<args...>::value <= 1, "*** Cannot contain more than one reference in arguments, since the reference is where we put the return value.");
			static_assert(CheckReferenceIsFirst<args...>::value, "*** Reference must be the first argument so it will be popped from the stack last (in fact, it is left in place).");
			static_assert( (CountReferences<args...>::value == 1) == std::is_same<T,void>::value, "*** If you have a reference, you must return void and vice versa.");
		}
		
	}
	
	
	/**
	 * @brief This gets called by a VirtualMachineState to evaluate the primitive on some arguments
	 * @param vms
	 * @param pool
	 * @param loader
	 * @return 
	 */	
	template<typename V, typename P, typename L>
	vmstatus_t VMScall(V* vms, P* pool, L* loader) {
		
		assert(not vms->template any_stacks_empty<typename std::decay<args>::type...>() &&
				"*** Somehow we ended up with empty arguments -- something is deeply wrong and you're in big trouble."); 
		
		// We let the compiler decide whether to push or not -- there is no push if we have a reference arg,
		// since that is left on the stack and we modify it. 		
		if constexpr (sizeof...(args) == 0) {
			// simple -- no arguments, no references, etc .
			vms->push(this->call());
		}
		else {
			
			// Ok here we deal with order of evaluation of nodes. We evaluate RIGHT to LEFT so that the 
			// rightmost args get popped from the stack first. This means that the last to be popped
			// is the first argument, which is why we allow references on the first arg. 
			// NOTE: we can't just do something like call(get<args>()...) because the order of evaluation
			// of that is not defined in C!
			
			// TODO: Replace the below with some template magic
			
			// deal with those references etc. 
			if constexpr (CountReferences<args...>::value == 0) {
				// if its not a reference, we just call normally
				// and push the result 
				if constexpr (sizeof...(args) ==  1) {
					auto a0 =  vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>();		
					vms->push(this->call(a0));
				}
				else if constexpr (sizeof...(args) ==  2) {
					auto a1 =  vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					auto a0 =  vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>();	
					vms->push(this->call(a0, a1));
				}
				else if constexpr (sizeof...(args) ==  3) {
					auto a2 =  vms->template get<typename std::tuple_element<2, std::tuple<args...> >::type>();
					auto a1 =  vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					auto a0 =  vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>();		
					vms->push(this->call(a0, a1, a2));
				}
				else if constexpr (sizeof...(args) ==  4) {
					auto a3 =  vms->template get<typename std::tuple_element<3, std::tuple<args...> >::type>();
					auto a2 =  vms->template get<typename std::tuple_element<2, std::tuple<args...> >::type>();
					auto a1 =  vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					auto a0 =  vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>();		
					vms->push(this->call(a0, a1, a2, a3));
				}
				else { assert(false && "*** VMScall not defined for >4 arguments -- you may add more cases in Primitives.h"); }
				
			}
			else { 
				// Same as above except we don't push and the a0 argument is a reference
				
				if constexpr (sizeof...(args) ==  1) {
					this->call(vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>());
				}
				else if constexpr (sizeof...(args) ==  2) {
					auto  a1 = vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					this->call(vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>(), a1);
				}
				else if constexpr (sizeof...(args) ==  3) {
					auto  a1 = vms->template get<typename std::tuple_element<2, std::tuple<args...> >::type>();
					auto  a2 = vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					this->call(vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>(), a1. a2);
				}
				else if constexpr (sizeof...(args) ==  4) {
					auto a3 =  vms->template get<typename std::tuple_element<3, std::tuple<args...> >::type>();
					auto a2 =  vms->template get<typename std::tuple_element<2, std::tuple<args...> >::type>();
					auto a1 =  vms->template get<typename std::tuple_element<1, std::tuple<args...> >::type>();
					this->call(vms->template get<typename std::tuple_element<0, std::tuple<args...> >::type>(), a1, a2. a3);
				}
				else { assert(false && "*** VMScall not defined for >4 arguments -- you may add more cases in Primitives.h"); }

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
	
	template<int n, class T, typename V, typename P, typename L>
	inline vmstatus_t applyToVMS_one(T& p, V* vms, P* pool, L* loader) {
		return std::get<n>(p).VMScall(vms, pool, loader);
	}

	template<class T, typename V, typename P, typename L, size_t... Is>
	inline vmstatus_t applyToVMS(T& p, int index, V* vms, P* pool, L* loader, std::index_sequence<Is...>) {
		using FT = vmstatus_t(T&, V*, P*, L*);
		thread_local static constexpr FT* arr[] = { &applyToVMS_one<Is, T, V, P, L>... }; //thread_local here seems to matter a lot
		return arr[index](p, vms, pool, loader);
	}
}
template<class T, typename V, typename P, typename L>
inline vmstatus_t applyToVMS(T& p, int index, V* vms, P* pool, L* loader) {
	
	// We need to put a check in here to ensure that nobody tries to do grammar.add(Primtive(...)) because
	// then it won't be included in our standard PRIMITIVES table, and so it cannot be called in this way
	// This is a problem in the library that should be addressed.
	assert( (size_t)index < std::tuple_size<T>::value && "*** You cannot index a higher primitive op than the size of PRIMITIVES. Perhaps you used grammar.add(Primitive(...)), which is not allowed, instead of putting it into the tuple?");
	assert(index >= 0);
	
	
    return Fleet::applyVMS::applyToVMS(p, index, vms, pool, loader, std::make_index_sequence<std::tuple_size<T>::value>{});

}


