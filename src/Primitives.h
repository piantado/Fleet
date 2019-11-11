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
	
// TODO: Make primtiive detect whether T is a VMS or not
// so we can give it a lambda of VMS if we wanted
//template<typename T, typename... args> // function type
//struct Primitive : PrePrimitive {
//	// A primitive associates a string name (format) with a function, 
//	// and allows grammars to extract all the relevant function pieces via constexpr,
//	// and also defines a VMS function that can be called in dispatch
//	// op here is generated uniquely for each Primitive, which is how LOTHypothesis 
//	// knows which to call. 
//	// SO: grammars use this op (statically updated) in nodes, and then when they 
//	// are linearized the op is processed in a switch statement to evaluate
//	// This is a funny intermediate type that essentially helps us associate 
//	// functions, formats, and nonterminal types all together. 
//	
//	std::string format;
//	T(*call)(args...); //call)(Args...);
//	vmstatus_t(*errcheck)(args...); //  error checking function -- if it returns anything other than vmstatus_t::GOOD, then that's returned
//	PrimitiveOp op;
//	double p;
//
//	Primitive(std::string fmt, T(*f)(args...), vmstatus_t(*e)(args...), double _p=1.0 ) :
//		format(fmt), call(f), errcheck(e), op(op_counter++), p(_p) {
//			
//		// check that each type here is in FLEET_GRAMMAR_TYPES
//		// or else we get obscure errors;
//		static_assert(contains_type<T,FLEET_GRAMMAR_TYPES>(), 
//					  "*** Type is not in FLEET_GRAMMAR_TYPES");
//		constexpr bool b = (contains_type<typename std::decay<args>::type,FLEET_GRAMMAR_TYPES>() && ...);
//		static_assert(b, "*** Type is not in FLEET_GRAMMAR_TYPES");
//	}
//	// We can construct without calling 
//	Primitive(std::string fmt, T(*f)(args...), double p=1.0 ) : Primitive(fmt, f, nullptr, p) {}
//	
//	template<typename V>
//	vmstatus_t VMScall(V* vms) {
//		// This is the default way of evaluating a PrimitiveOp
//		
//		assert(not vms->template any_stacks_empty<args...>() &&
//			   "*** Somehow we ended up with empty arguments -- something is deeply wrong."); 
//		
//		// we'll get this from the top references (which leaves them on the check
//		// TODO: NOTE: This gettop is not great because we make copies intead of leaving them on the stacks
//
//		// TODO: We can do errcheck here except that we need to manage what we get back from the stack
//		// because if we pop here, 
////		auto check = (errcheck == nullptr ? vmstatus_t::GOOD : errcheck(vms->template getpop<args>()...));
////		if(check != vmstatus_t::GOOD) return check;
//		
//		auto ret = this->call(vms->template getpop<args>()...);		
//		vms->push(ret);
//				
//		return vmstatus_t::GOOD;
//	}
//	
//};



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
	T(*call)(args...); //call)(Args...);
	vmstatus_t(*errcheck)(args...); //  error checking function -- if it returns anything other than vmstatus_t::GOOD, then that's returned
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
		// (ii) the reference is the LAST of its type in args (otherwise, the reference would be popped off the
		// stack in evaluating the expression. 
		
		
		// TODO: ALSO ASSERT that the return type is the same as the reference (otherwise the grammar doesn't know what ot do)
		// BUT Note we shouldn't actually return
		if constexpr(sizeof...(args) > 0) {
			static_assert(CountReferences<args...>::value <= 1, "*** Cannot contain more than one reference in arguments, since the reference is where we put the return value.");
			static_assert(CheckReferenceIsLastOfItsType<args...>::value, "*** Reference must be the last of its type in args, or else it will be popped from the stack.");
 		}
//		static_assert(CountReferences<args...>::value == 0 or std::is_void<T>::value, "*** If you use a reference, returntype must be the same -- though note you won't actually return anything");
		
	}
	// We can construct without calling 
//	Primitive(std::string fmt, T(*f)(args...), double p=1.0 ) : Primitive(fmt, f, nullptr, p) {}
	
	
	
	template<typename V>
	vmstatus_t VMScall(V* vms) {
		// This is the default way of evaluating a PrimitiveOp
		
		assert(not vms->template any_stacks_empty<typename std::decay<args>::type...>() &&
				"*** Somehow we ended up with empty arguments -- something is deeply wrong and you're in big trouble."); 
		
		// we'll get this from the top references (which leaves them on the check
		//auto check = (errcheck == nullptr ? vmstatus_t::GOOD : errcheck(vms->template get<args>()...));
		//if(check != vmstatus_t::GOOD) return check;
		
		
		
		
//		vms->push(this->call(vms->template get<args>()...));
		
		
		if constexpr (sizeof...(args) == 0) {
			// simple -- no arguments, no references, etc .
			vms->push(this->call());
		}
		else {
			// deal with those references etc. 
			if constexpr (CountReferences<args...>::value == 0) {
				// if its not a reference, we 
				vms->push(this->call(vms->template get<args>()...));
			}
			else { 
				// don't push -- we assuming the reference handled the return value
				this->call(vms->template get<args>()...);
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