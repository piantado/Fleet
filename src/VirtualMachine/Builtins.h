#pragma once 

// These are convenient wrappers for built-in operations that just bundle toegether format, op, and p
// this is useful because then we can put the builtins in the same PRIMITIVES bundle
// so for instance we can have Primitives::If<S>("if(%s,%s,%s)", 1.0) in PRIMITIVES, even though If is a built-in
// So these are just data structures that hold this information until grammar.add, where they are
// mapped onto Rules for builtins

template<typename t, typename... args>
struct BuiltinPrimitive {
	// This is just an interface that wraps builtins so we can add them to our Tuples primitive
	// It does NOT need to inherit from PrePrimitive since these shuold have builtin op numbers
	std::string format; 
	BuiltinOp op;
	double p;
	
	template<typename V, typename P, typename L>
	vmstatus_t VMScall(V* vms, P* pool, L* loader) { assert(0); } // must be defined but not used 
};

namespace Builtin {

	template<typename t> struct If : public BuiltinPrimitive<t,bool,t,t> {
		If(std::string fmt, double _p=1.0) : BuiltinPrimitive<t,bool,t,t>{fmt, BuiltinOp::op_IF, _p} { }		
	};
	
	template<typename t> struct X : public BuiltinPrimitive<t> {
		X(std::string fmt, double _p=1.0) : BuiltinPrimitive<t>{fmt, BuiltinOp::op_X, _p} { }		
	};
	
	struct Flip : public BuiltinPrimitive<bool> {
		Flip(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool>{fmt, BuiltinOp::op_FLIP, _p} { }		
	};
	
	struct FlipP : public BuiltinPrimitive<bool, double> {
		FlipP(std::string fmt, double _p=1.0) : BuiltinPrimitive<bool, double>{fmt, BuiltinOp::op_FLIPP, _p} { }		
	};
	
	template<typename t_out, typename t_in> struct Recurse : public BuiltinPrimitive<t_out, t_in> {
		Recurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_RECURSE, _p} { }		
	};
	
	template<typename t_out, typename t_in> struct SafeRecurse : public BuiltinPrimitive<t_out, t_in> {
		SafeRecurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_SAFE_RECURSE, _p} { }		
	};

	template<typename t_out, typename t_in> struct SafeMemRecurse : public BuiltinPrimitive<t_out, t_in> {
		SafeMemRecurse(std::string fmt, double _p=1.0) : BuiltinPrimitive<t_out, t_in>{fmt, BuiltinOp::op_SAFE_MEM_RECURSE, _p} { }
	};
}
