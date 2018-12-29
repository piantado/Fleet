#pragma once

#include <variant>

class Instruction { 
public:
		// This is an operation in a program, which can store either a BuiltinOp or a CustomOp
		// It is evaluated by dispatch and created from nodes by linearize, and in Rule's constructor.
		// it has a flag (is_custom) to say whether it's a builtin or custom op. Some of the builtin ops
		// take arguments (like jump) 
		// The "arg" here is super useful since it essentially allows us to define classes of instructions
		// for instance, jump takes an arg, factorized recursion use sarg for index, in FormalLanguageTheory
		// we use arg to store which alphabet terminal, etc. 
		
		std::variant<CustomOp, BuiltinOp> op; // what kind of op is this? custom or built in?
		int                              arg; // 
		
		// constructors to make this a little easier to deal with
		Instruction(BuiltinOp x, int arg_=0x0) : op(x), arg(arg_) {
		}
		Instruction(CustomOp x, int arg_=0x0)  : op(x), arg(arg_)  {
		}
		
		bool is_custom() const {
			return std::holds_alternative<CustomOp>(op);
		}
		
		bool is_builtin() const {
			return std::holds_alternative<BuiltinOp>(op);
		}
		
		CustomOp getCustom() const { // will throw if op isn't storing custom
			return std::get<CustomOp>(op);
		}
		
		BuiltinOp getBuiltin() const { // will throw if op isn't storing custom
			return std::get<BuiltinOp>(op); 
		}

		int getArg() const {
			return arg; 
		}
		
		// compare our ops, ignoring the arg
		bool is_a(const CustomOp c) const {
			return is_custom() && getCustom() == c;
		}
		bool is_a(const BuiltinOp b) const {
			return (!is_custom()) && getBuiltin() == b;
		}
		

		template<typename T, typename... Ts>
		bool is_a(T x, Ts... args) const {
			// we defaulty allow is_a to take a list of possible ops, either builtin or custom,
			// and returns true if ANY of the types are matched
			return is_a(x) || is_a(args...);
		}
		
};


std::ostream& operator<<(std::ostream& stream, Instruction& i) {
	stream << "[" << (i.is_custom()?"C":"B") << " " << (i.is_custom()?(int)i.getCustom():(int)i.getBuiltin()) << "\t" << i.arg << "]";
	return stream;
}

//
//typedef struct Instruction { 
//		// This is an operation in a program, which can store either a BuiltinOp or a CustomOp
//		// It is evaluated by dispatch and created from nodes by linearize, and in Rule's constructor.
//		// it has a flag (is_custom) to say whether it's a builtin or custom op. Some of the builtin ops
//		// take arguments (like jump) 
//		// The "arg" here is super useful since it essentially allows us to define classes of instructions
//		// for instance, jump takes an arg, factorized recursion use sarg for index, in FormalLanguageTheory
//		// we use arg to store which alphabet terminal, etc. 
//		
//		bool         amicustom  : 1; // what kind of op is this? custom or built in?
//		CustomOp     custom     : 8; // custom ops go here
//		BuiltinOp    builtin    : 8; // if we are built in, which op is it?
//		int          arg        : 14; // 
//		
//		// constructors to make this a little easier to deal with
//		Instruction(BuiltinOp x, int arg_=0x0) :
//			amicustom(false), custom((CustomOp)0x0), builtin(x), arg(arg_) {
//		}
//		Instruction(CustomOp x, int arg_=0x0) :
//			amicustom(true), custom(x), builtin((BuiltinOp)0x0), arg(arg_) {
//		}
//		
//		bool is_custom() const {
//			return amicustom;
//		}
//		
//		bool is_builtin() const {
//			return !amicustom;
//		}
//		
//		CustomOp getCustom() const { // will throw if op isn't storing custom
//			return custom;
//		}
//		
//		BuiltinOp getBuiltin() const { // will throw if op isn't storing custom
//			return builtin;
//		}
//		
//		int getArg() const {
//			return arg; 
//		}
//		
//		// compare our ops, ignoring the arg
//		bool is_a(const CustomOp c) const {
//			return amicustom && custom == c;
//		}
//		bool is_a(const BuiltinOp b) const {
//			return (!amicustom) && builtin == b;
//		}
//		
//		bool is_a(const BuiltinOp b1, const BuiltinOp b2) const {
//			return is_a(b1) || is_a(b2);
//		}
//		
//} Instruction;