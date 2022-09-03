#pragma once 

enum class Op {
	Standard, 
	And, Or, Not, Implies, Iff,
	X,
	If, Jmp,
	Recurse, PopX, SafeRecurse, MemRecurse, SafeMemRecurse,
	LexiconRecurse, LexiconSafeRecurse, LexiconMemRecurse, LexiconSafeMemRecurse,
	Flip, FlipP, SafeFlipP,
	Sample,
	Mem,
	NoOp,
	CL_I, CL_S, CL_K, CL_Apply,
	Custom1, Custom2, Custom3, Custom4, Custom5 
};

