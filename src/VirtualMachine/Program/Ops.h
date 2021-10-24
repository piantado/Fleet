#pragma once 

enum class Op {
	Standard, 
	And, Or, Not, 
	X,
	If, Jmp,
	Recurse, PopX, SafeRecurse, MemRecurse, SafeMemRecurse,
	LexiconRecurse, LexiconSafeRecurse, LexiconMemRecurse, LexiconSafeMemRecurse,
	Flip, FlipP, SafeFlipP,
	Mem,
	NoOp,
	CL_I, CL_S, CL_K, CL_Apply
};

