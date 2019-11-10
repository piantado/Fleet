#pragma once




// TODO: ADD ERR CATCHES




#include <string>
#include <cstdlib>
#include <functional>

struct PrePrimitive {
	// need to define something all Primtiives inherit from so that
	// we can share op_counters across them	
	static size_t op_counter; // this gives each primitive we define a unique identifier	
};
size_t PrePrimitive::op_counter = 0;
	
// TODO: Make primtiive detect whether T is a VMS or not
// so we can give it a lambda of VMS if we wanted
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
	size_t op;
	double p;
	
	Primitive(std::string fmt, T(*f)(args...), double _p=1.0 ) :
		format(fmt), call(f), op(op_counter++), p(_p) {
			
		// check that each type here is in FLEET_GRAMMAR_TYPES
		// or else we get obscure errors;
		static_assert(contains_type<T,FLEET_GRAMMAR_TYPES>(), 
					  "*** Type is not in FLEET_GRAMMAR_TYPES");
		constexpr bool b = (contains_type<args,FLEET_GRAMMAR_TYPES>() && ...);
		static_assert(b, "*** Type is not in FLEET_GRAMMAR_TYPES");
			
	}
	
	Primitive(std::string fmt, BuiltinOp o, double _p=1.0) : format(fmt), op((size_t)o), p(_p) {
	}
	
	template<typename V>
	abort_t VMScall(V* vms) {
		// a way of calling from a VMS if we want it
		
		// Can't expand liek this because its only for arguments, not 
		assert(not vms->template _stacks_empty<args...>()); // TODO: Implement this... -- need to define empty for variadic TYPES, not arguments...
		
		auto ret = this->call(vms->template getpop<args>()...);
		
		vms->push(ret);
				
		return abort_t::NO_ABORT;
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

	template<int N, class T, class V>
	abort_t applyToVMS_one(T& p, V vms) {
		return std::get<N>(p).VMScall(vms);
	}

	template<class T, class V, int... Is>
	abort_t applyToVMS(T& p, int index, V func, seq<Is...>) {
		using FT = abort_t(T&, V);
		static constexpr FT* arr[] = { &applyToVMS_one<Is, T, V>... };
		return arr[index](p, func);
	}
}
template<class T, class V>
abort_t applyToVMS(T& p, int index, V vms) {
    return Fleet::applyVMS::applyToVMS(p, index, vms, Fleet::applyVMS::gen_seq<std::tuple_size<T>::value>{});
	// TODO: I think we can probably get away without gen_seq, using something like below
//    return Fleet::applyVMS::applyToVMS(p, index, vms, std::make_integer_sequence<std::tuple_size<T>>{});

}