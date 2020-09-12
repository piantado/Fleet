#pragma once 

// These operations are build-in and implemented in VirtualMachineState
// convenient to make op_NOP=0, so that the default initialization is a NOP
enum class BuiltinOp {
	op_NOP=0,op_X,op_POPX,
	op_MEM,op_RECURSE,op_MEM_RECURSE, // thee can store the index of what hte loader calls in arg, so they can be used with lexica if you pass arg
	op_SAFE_RECURSE, op_SAFE_MEM_RECURSE,
	op_FLIP,op_FLIPP,op_IF,op_JMP,
	op_TRUE,op_FALSE,
	op_AND,op_OR,op_NOT, // these are short circuit versions
	op_ALPHABET,op_ALPHABETchar, // strings by default, also can do chars
	op_INT,// decode integers from arg
	op_FLOAT,
	op_P, // probability 
	op_SKAPPLY, op_S, op_K, op_I // for combinatory logic
	//op_LAMBDA,op_APPLY // simple, one-argument lambda functions (as in forall)
};

// Not sure Where to put this...
static int Pdenom = 24; // the denominator for probabilities in op_P --  we're going to enumerate fractions in 24ths -- just so we can get thirds, quarters, fourths		

// PrimitiveOps are defined by the PRIMITIVES global variable (if we want one)
// and then they each are automatically given a type number, which is here
// defined just to be short
typedef short PrimitiveOp;

