#pragma once

/* This contains macros for the dispatch case that allow easier definitions of case statements. 
 * These extract from the stack in a VirtualMachineState and pass arguments so that we may 
 * define these case statemetns with functions. 
 * 
 * Here, we extract from the stack in the right order. If we have an operator like (+ x y), 
 * then linearize gives a stack TOP y x + for the program, then when we evaluate y and x
 * they get pushed onto a stack of intermediate values in reverse order TOP x y
 * that the first pop is x, the next is y 
 * */


//Wow c++ uses this insane syntax for class member templates
#define CASE_FUNC0(opcode, returntype, f)                                       \
	case opcode: {                                                              \
		vms.template push<returntype>(std::move(f()));                                     \
		break;									                                \
	}                                                                           \
	
#define CASE_FUNC1(opcode, returntype, a1type, f)                               \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		vms.template push<returntype>(std::move(f(a1)));                                   \
		break;									                                \
	}                                                                           \
	
#define CASE_FUNC2(opcode, returntype, a1type, a2type, f)                       \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		vms.template push<returntype>(std::move(f(a1,a2)));                                \
		break;									                                \
	}                                                                           \

#define CASE_FUNC3(opcode, returntype, a1type, a2type, a3type, f)               \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		a3type a3 = vms.template getpop<a3type>();                              \
		vms.template push<returntype>(std::move(f(a1,a2,a3)));                             \
		break;									                                \
	}                                                                           \



#define CASE_FUNC4(opcode, returntype, a1type, a2type, a3type, a4type, f)               \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		a3type a3 = vms.template getpop<a3type>();                              \
		a4type a4 = vms.template getpop<a4type>();                              \
		vms.template push<returntype>(std::move(f(a1,a2,a3,a4)));                             \
		break;									                                \
	}   
/* We also define types that do error handling.
 * Here, errcheck is a function that takes the same arguments and if it returns
 * nonzero, that is the error code
 * */

#define CASE_FUNC0e(opcode, returntype, f, errcheck)                            \
	case opcode: {                                                              \
		abort_t e = errcheck();									                \
		if(e != abort_t::GOOD) return e;								                \
		vms.template push<returntype>(std::move(f()));                                     \
		break;									                                \
	}                                                                           \
	
#define CASE_FUNC1e(opcode, returntype, a1type, f, errcheck)                    \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		abort_t e = errcheck(a1);									            \
		if(e != abort_t::GOOD) return e;								                \
		vms.template push<returntype>(std::move(f(a1)));                                   \
		break;									                                \
	}                                                                           \
	
#define CASE_FUNC2e(opcode, returntype, a1type, a2type, f, errcheck)            \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		abort_t e = errcheck(a1,a2);									        \
		if(e != abort_t::GOOD) return e;								                \
		vms.template push<returntype>(std::move(f(a1,a2)));                                \
		break;									                                \
	}                                                                           \

#define CASE_FUNC3e(opcode, returntype, a1type, a2type, a3type, f, errcheck)    \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		a3type a3 = vms.template getpop<a3type>();                              \
		abort_t e = errcheck(a1,a2,a3);									        \
		if(e != abort_t::GOOD) return e;								                \
		vms.template push<returntype>(std::move(f(a1,a2,a3)));                             \
		break;									                                \
	}                                                                           \

#define CASE_FUNC4e(opcode, returntype, a1type, a2type, a3type, a4type, f, errcheck)    \
	case opcode: {                                                              \
		a1type a1 = vms.template getpop<a1type>();                              \
		a2type a2 = vms.template getpop<a2type>();                              \
		a3type a3 = vms.template getpop<a3type>();                              \
		a4type a4 = vms.template getpop<a4type>();                              \
		abort_t e = errcheck(a1,a2,a3,a4);									        \
		if(e != abort_t::GOOD) return e;								                \
		vms.template push<returntype>(std::move(f(a1,a2,a3,a4)));                             \
		break;									                                \
	}                                                                           \



 
	