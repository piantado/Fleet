#pragma once

typedef struct Instruction { 
		// This is an operation in a program, which can store either a BuiltinOp or a CustomOp
		// It is evaluated by dispatch and created from nodes by linearize, and in Rule's constructor.
		// it has a flag (is_custom) to say whether it's a builtin or custom op. Some of the builtin ops
		// take arguments (like jump) 
		// The "arg" here is super useful since it essentially allows us to define classes of instructions
		// for instance, jump takes an arg, factorized recursion use sarg for index, in FormalLanguageTheory
		// we use arg to store which alphabet terminal, etc. 
		
		bool         is_custom  : 1; // what kind of op is this? custom or built in?
		CustomOp     custom     : 8; // custom ops go here
		BuiltinOp    builtin    : 6; // if we are built in, which op is it?
		int          arg        : 16; // 
		
		// constructors to make this a little easier to deal with
		Instruction(BuiltinOp x, int arg_=0x0) :
			is_custom(false), custom((CustomOp)0x0), builtin(x), arg(arg_) {
		}
		Instruction(CustomOp x, int arg_=0x0) :
			is_custom(true), custom(x), builtin((BuiltinOp)0x0), arg(arg_) {
		}
		
		bool operator==(Instruction i) const {
			// equality checking, but tossing the bits we don't need based on whether is_custom
			if(! (is_custom == i.is_custom && arg == i.arg)) return false; // these always must match
			if(is_custom) return custom  == i.custom;
			else          return builtin == i.builtin;
		}
		
		// compare our ops, ignoring the arg
		bool operator==(CustomOp c) {
			return is_custom && custom == c;
		}
		bool operator==(BuiltinOp b) {
			return (!is_custom) && builtin == b;
		}
		
} Instruction;

std::ostream& operator<<(std::ostream& stream, const Instruction& i) {
	stream << "[" << (i.is_custom?"C":"B") << " " << (i.is_custom?(int)i.custom:(int)i.builtin) << "\t" << i.arg << "]";
	return stream;
}
