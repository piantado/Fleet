#pragma once

#include <iostream>
#include <variant>

// PrimitiveOps are defined by the PRIMITIVES global variable (if we want one)
// and then they each are automatically given a type number, which is here
// defined just to be short
typedef short PrimitiveOp;

// This class stores the possible statuses of a VirtualMachine
enum class vmstatus_t {GOOD=0, ERROR, RECURSION_DEPTH, RANDOM_CHOICE, RANDOM_CHOICE_NO_DELETE, SIZE_EXCEPTION, OP_ERR_ABORT, RANDOM_BREAKOUT}; // setting GOOD=0 allows us to say if(aborted)...

/**
 * @class VMSRuntimeError_t
 * @author steven piantadosi
 * @date 03/02/20
 * @file Instruction.h
 * @brief This is an error type that is returned if we get a runtime error (e.g. string length, etc.)
 */
class VMSRuntimeError_t : public std::exception {} VMSRuntimeError;


#ifndef CUSTOM_OPS
#define CUSTOM_OPS 
#endif 


enum class CustomOp { CUSTOM_OPS };

// These operations are build-in and implemented in VirtualMachineState
// convenient to make op_NOP=0, so that the default initialization is a NOP
enum class BuiltinOp {
	op_NOP=0,op_X,op_POPX,
	op_MEM,op_RECURSE,op_MEM_RECURSE, // thee can store the index of what hte loader calls in arg, so they can be used with lexica if you pass arg
	op_SAFE_RECURSE, op_SAFE_MEM_RECURSE,
	op_FLIP,op_FLIPP,op_IF,op_JMP,
	op_TRUE,op_FALSE,
	op_ALPHABET,op_ALPHABETchar, // strings by default, also can do chars
	op_INT,// decode integers from arg
	op_P // probability 
	//op_LAMBDA,op_APPLY // simple, one-argument lambda functions (as in forall)
};


/**
* @class Instruction
* @author piantado
* @date 29/01/20
* @file Instruction.h
* @brief  This is how we store an instruction in the program. It can take one of three types:
	 BuiltinOp -- these are operations that are defined as part of Fleet's core library,
					and are implemented automatically in VirtualMachineState::run
	 PrimitiveOp -- these are defined *automatically* through the global variable PRIMITIVES
					  (see Models/RationalRules). When you construct a grammar with these, it automatically
					  figures out all the types and automatically gives each a sequential numbering, which
					  it takes some template magic to access at runtime
	 CustomOp -- these are for when you need more access to VMS' stack, and they require you to implement
				   custom_dispatch (where the instruction is handled)

	 For any op type, an Instruction always takes an "arg" type that essentially allow us to define classes of instructions
	 for instance, jump takes an arg, factorized recursion uses arg for index, in FormalLanguageTheory
	 we use arg to store which alphabet terminal, etc. 

*/
class Instruction { 
public:

	std::variant<BuiltinOp, 
				 CustomOp,
				 PrimitiveOp> op; // what kind of op is this? custom or built in?
	int                       arg; // 

	// constructors to make this a little easier to deal with
	Instruction()                            : op(BuiltinOp::op_NOP), arg(0x0) {}
	Instruction(BuiltinOp x,    int arg_=0x0) : op(x), arg(arg_)  { }
	Instruction(CustomOp x,    int arg_=0x0) : op(x), arg(arg_)  { }
	Instruction(PrimitiveOp x, int arg_=0x0) : op(x), arg(arg_) { }

	template<typename t>
	bool is() const {
		/**
		 * @brief Template to check if this instruction is holding type t
		 * @return 
		 */
		return std::holds_alternative<t>(op);
	}

	template<typename t>
	t as() const {
		/**
		 * @brief Get as type t
		 * @return 
		 */
		
		assert(is<t>() && "*** Something is very wrong if we can't get it as type t");
		return std::get<t>(op);
	}

	int getArg() const {
		/**
		 * @brief Return the argument (an int)
		 * @return 
		 */
		
		return arg; 
	}
	
	bool operator==(const Instruction& i) const {
		return op==i.op and arg==i.arg;
	}

	/// compare the instruction types (ignores the arg)
	template<typename T>
	bool is_a(const T x)    const {
		return (is<T>() and as<T>() == x);
	}
	template<typename T, typename... Ts>
	bool is_a(T x, Ts... args) const {
		/**
		 * @brief Variadic checking of whether this is a given op type
		 * @param x
		 * @return 
		 */
				
		// we defaulty allow is_a to take a list of possible ops, either builtin or custom,
		// and returns true if ANY of the types are matched
		return is_a(x) || is_a(args...);
	}
		
};


std::ostream& operator<<(std::ostream& stream, Instruction& i) {
	/**
	 * @brief Output for instructions. 
	 * @param stream
	 * @param i
	 * @return 
	 */
	
	std::string t;
	size_t      o;
	if(i.is<BuiltinOp>())          { t = "B"; o = (size_t)i.as<BuiltinOp>(); }
	else if(i.is<PrimitiveOp>())   { t = "P"; o = (size_t)i.as<PrimitiveOp>(); }
	
	
	stream << "[" << t << "" << o << "_" << i.arg << "]";
	return stream;
}
